#include "Capture/CaptureWriter.h"
#include "AppRuntimeLog.h"

#include <deque>
#include <format>
#include <random>
#include <utility>

namespace gglab
{
	struct CaptureWriter::State
	{
		FrameCaptureImageEncoder m_Encoder;
		std::shared_ptr<CapturePublicationFileSystemBase> m_Files;
		std::string m_TemporaryTag;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		std::deque<std::shared_ptr<CaptureWriteJob>> m_Queue;
		bool m_Stopping = false;
	};

	namespace
	{
		[[nodiscard]] std::string CreateTemporaryTag() noexcept
		{
			uint64_t value = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			try
			{
				std::random_device device;
				value ^= (static_cast<uint64_t>(device()) << 32) | device();
			}
			catch (...)
			{
			}
			return std::format("{:016x}", value);
		}
	}

	std::optional<CaptureWriteReceipt> CaptureWriteJob::GetReceipt() const noexcept
	{
		std::scoped_lock lock(m_Mutex);
		return m_Receipt;
	}

	void CaptureWriteJob::WaitUntil(std::chrono::steady_clock::time_point deadline) noexcept
	{
		std::unique_lock lock(m_Mutex);
		m_Condition.wait_until(lock, deadline, [this]() { return m_Receipt.has_value(); });
	}

	CaptureWriter::CaptureWriter(FrameCaptureImageEncoder encoder, bool writeOnCallingThread,
		std::shared_ptr<CapturePublicationFileSystemBase> files) noexcept :
		m_State(std::make_shared<State>()), m_WriteOnCallingThread(writeOnCallingThread)
	{
		m_State->m_Encoder = std::move(encoder);
		m_State->m_Files = files ? std::move(files) : CreateCapturePublicationFileSystem();
		m_State->m_TemporaryTag = CreateTemporaryTag();
		if (!m_WriteOnCallingThread)
		{
			try
			{
				m_Thread = std::thread(&CaptureWriter::Run, m_State);
			}
			catch (...)
			{
				GGLAB_LOG_WARN_ALWAYS(
					"The frame capture writer thread could not start; captures are written on the frame thread.");
				m_WriteOnCallingThread = true;
			}
		}
	}

	CaptureWriter::~CaptureWriter()
	{
		if (!m_Thread.joinable())
		{
			return;
		}
		{
			std::scoped_lock lock(m_State->m_Mutex);
			m_State->m_Stopping = true;
		}
		m_State->m_Condition.notify_all();
		// File I/O cannot be interrupted. Detached state owns all capabilities and
		// immutable CPU products, never a coordinator, Runtime service or device.
		if (m_HasAbandonedJobs)
		{
			m_Thread.detach();
		}
		else
		{
			m_Thread.join();
		}
	}

	bool CaptureWriter::Submit(std::shared_ptr<CaptureWriteJob> job) noexcept
	{
		job->m_SubmittedAt = std::chrono::steady_clock::now();
		if (m_WriteOnCallingThread)
		{
			Write(*job, *m_State);
			return true;
		}
		{
			std::scoped_lock lock(m_State->m_Mutex);
			// All nine reservations can reach the queue before the worker claims
			// its first job. Once one runs, at most eight others remain pending.
			if (m_State->m_Stopping || m_State->m_Queue.size() >= MaxInFlightJobs)
			{
				return false;
			}
			m_State->m_Queue.push_back(std::move(job));
		}
		m_State->m_Condition.notify_one();
		return true;
	}

	CaptureWriteReceipt CaptureWriter::Abandon(CaptureWriteJob& job,
		std::chrono::milliseconds timeout) noexcept
	{
		if (auto receipt = job.GetReceipt())
		{
			return std::move(*receipt);
		}
		const auto stage = job.m_Publication.m_Stage.load();
		if (job.m_Publication.TryAbandon())
		{
			m_HasAbandonedJobs = true;
			return { .m_Failure = std::format(
				"Writing did not finish within {} ms of shutdown (stage: {}); the capture "
				"was abandoned and its files are not published.", timeout.count(),
				GetCaptureWriteStageName(stage)) };
		}
		if (auto receipt = job.GetReceipt())
		{
			return std::move(*receipt);
		}
		m_HasAbandonedJobs = true;
		return { .m_Publication = job.m_Publication.GetUnfinishedOutcome(),
			.m_Failure = std::format(
				"Publishing did not finish within {} ms of shutdown; capture publication "
				"is indeterminate and files may already exist or appear later.", timeout.count()) };
	}

	void CaptureWriter::Run(std::shared_ptr<State> state) noexcept
	{
		while (true)
		{
			std::shared_ptr<CaptureWriteJob> job;
			{
				std::unique_lock lock(state->m_Mutex);
				state->m_Condition.wait(lock,
					[&state]() { return state->m_Stopping || !state->m_Queue.empty(); });
				if (state->m_Queue.empty())
				{
					return;
				}
				job = std::move(state->m_Queue.front());
				state->m_Queue.pop_front();
			}
			Write(*job, *state);
		}
	}

	void CaptureWriter::Write(CaptureWriteJob& job, const State& state) noexcept
	{
		CaptureWriteReceipt receipt;
		{
			// Encoded scratch, aliased array storage and path serializers all die
			// before the input products are released and completion becomes visible.
			CaptureEncodedProduct encoded;
			if (job.m_Publication.IsAbandoned())
			{
				receipt.m_Failure = "The capture was abandoned at shutdown.";
			}
			else
			{
				encoded = EncodeCaptureProduct(job.m_Products, state.m_Encoder, job.m_Publication.m_Stage);
				receipt.m_Failure = std::move(encoded.m_Failure);
				if (receipt.m_Failure.empty())
				{
					const std::string temporaryStem = std::format("{}.{}.{}",
						job.m_BaseStem, state.m_TemporaryTag, job.m_RequestId);
					auto publication = PublishCaptureFiles(encoded.m_Content, job.m_Directory,
						job.m_BaseStem, temporaryStem, job.m_Publication, *state.m_Files);
					receipt.m_Publication = publication.m_Outcome;
					receipt.m_Failure = std::move(publication.m_Failure);
					if (publication.m_Outcome == CapturePublicationOutcome::Published)
					{
						receipt.m_MetadataPath = publication.m_Files.back();
						if (encoded.m_ImageIndex)
						{
							receipt.m_ImagePath = publication.m_Files[*encoded.m_ImageIndex];
						}
						if (encoded.m_GraphIndex)
						{
							receipt.m_GraphPath = publication.m_Files[*encoded.m_GraphIndex];
						}
						receipt.m_ResourcePaths.assign(publication.m_Files.begin(),
							publication.m_Files.begin() + encoded.m_ResourceCount);
					}
				}
			}
		}
		job.m_Products = {};
		job.m_Publication.m_Stage = CaptureWriteStage::Done;
		{
			std::scoped_lock lock(job.m_Mutex);
			job.m_Receipt = std::move(receipt);
		}
		job.m_Condition.notify_all();
	}
}
