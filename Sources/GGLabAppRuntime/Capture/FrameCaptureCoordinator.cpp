#include "Capture/FrameCaptureCoordinator.h"

#include "AppRuntimeLog.h"
#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureControlBase.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureImageEncoding.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <format>
#include <fstream>
#include <limits>
#include <mutex>
#include <span>
#include <stop_token>
#include <system_error>
#include <utility>

namespace gglab
{
	struct FrameCaptureCoordinator::EncodeJob
	{
		std::shared_ptr<const FrameCaptureImage> m_Image;
		std::filesystem::path m_ImagePath;
		std::filesystem::path m_MetadataPath;
		std::string m_MetadataJson;

		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_IsDone = false;
		std::string m_Failure;
	};

	namespace
	{
		// Bounds the shutdown wait for an encode task that never ran.
		constexpr std::chrono::seconds EncodeShutdownTimeout{ 30 };
		constexpr size_t MaxFileStemComponentLength = 64;

		// Writes the bytes to a sibling temporary file and renames it into place,
		// so observers never see a partially written output.
		[[nodiscard]] std::string WriteFileAtomically(
			const std::filesystem::path& path, std::span<const uint8_t> bytes) noexcept
		{
			std::error_code errorCode;
			std::filesystem::create_directories(path.parent_path(), errorCode);
			if (!std::filesystem::is_directory(path.parent_path(), errorCode))
			{
				return std::format("The output directory '{}' could not be created.",
					path.parent_path().string());
			}

			std::filesystem::path temporaryPath = path;
			temporaryPath += L".partial";
			{
				std::ofstream file(temporaryPath, std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char*>(bytes.data()),
					static_cast<std::streamsize>(bytes.size()));
				file.close();
				if (!file)
				{
					std::filesystem::remove(temporaryPath, errorCode);
					return std::format("Writing '{}' failed.", temporaryPath.string());
				}
			}
			std::filesystem::rename(temporaryPath, path, errorCode);
			if (errorCode)
			{
				std::filesystem::remove(temporaryPath, errorCode);
				return std::format("Publishing '{}' failed.", path.string());
			}
			return {};
		}

		[[nodiscard]] std::string EncodeAndWrite(
			const std::shared_ptr<const FrameCaptureImage>& image,
			const std::filesystem::path& imagePath, const std::filesystem::path& metadataPath,
			const std::string& metadataJson) noexcept
		{
			const std::optional<std::vector<uint8_t>> png =
				image ? EncodeFrameCapturePng(*image) : std::nullopt;
			if (!png)
			{
				return "The captured image could not be encoded as PNG.";
			}
			std::string failure = WriteFileAtomically(imagePath, *png);
			if (!failure.empty())
			{
				return failure;
			}
			const auto* jsonBytes = reinterpret_cast<const uint8_t*>(metadataJson.data());
			failure = WriteFileAtomically(
				metadataPath, std::span<const uint8_t>(jsonBytes, metadataJson.size()));
			if (!failure.empty())
			{
				// A capture is complete only with both files.
				std::error_code errorCode;
				std::filesystem::remove(imagePath, errorCode);
			}
			return failure;
		}

		[[nodiscard]] std::string SanitizeFileStemComponent(std::string_view value) noexcept
		{
			std::string result;
			for (const char character : value)
			{
				if (result.size() >= MaxFileStemComponentLength)
				{
					break;
				}
				const bool allowed = (character >= 'a' && character <= 'z') ||
					(character >= 'A' && character <= 'Z') ||
					(character >= '0' && character <= '9') || character == '-' ||
					character == '_' || character == '.';
				result += allowed ? character : '_';
			}
			return result.empty() ? std::string("capture") : result;
		}
	}

	FrameCaptureCoordinator::FrameCaptureCoordinator(const CreateInfo& createInfo) noexcept :
		m_Capture(createInfo.m_Capture),
		m_TaskSystem(createInfo.m_TaskSystem),
		m_DefaultOutputDirectory(createInfo.m_DefaultOutputDirectory)
	{
	}

	FrameCaptureCoordinator::~FrameCaptureCoordinator()
	{
		// Encode tasks own their job state, so an unfinished job cannot outlive
		// its data; the coordinator only waits to keep output publication ordered.
		CollectEncodedJobs(true);
	}

	uint64_t FrameCaptureCoordinator::Submit(FrameCaptureRequest request) noexcept
	{
		Entry& entry = m_Entries.emplace_back();
		entry.m_Id = m_NextRequestId++;
		entry.m_Request = std::move(request);
		if (m_IsShuttingDown)
		{
			Finish(entry, FrameCaptureRequestStatus::Cancelled,
				"The runtime is shutting down.");
		}
		return entry.m_Id;
	}

	bool FrameCaptureCoordinator::Cancel(uint64_t requestId) noexcept
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Id == requestId && entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled, "Cancelled by request.");
				return true;
			}
		}
		return false;
	}

	void FrameCaptureCoordinator::ConsumeResults(
		std::vector<FrameCaptureRequestResult>& outResults) noexcept
	{
		RemoveFinishedEntries();
		outResults.insert(outResults.end(), std::make_move_iterator(m_Results.begin()),
			std::make_move_iterator(m_Results.end()));
		m_Results.clear();
	}

	uint32_t FrameCaptureCoordinator::GetUnfinishedRequestCount() const noexcept
	{
		return static_cast<uint32_t>(std::ranges::count_if(m_Entries,
			[](const Entry& entry) { return entry.m_Phase != Phase::Finished; }));
	}

	const FrameCaptureFrameState* FrameCaptureCoordinator::GetLastFrameState() const noexcept
	{
		return m_LastFrameState ? &*m_LastFrameState : nullptr;
	}

	void FrameCaptureCoordinator::BeginFrame(FrameCaptureFrameState state) noexcept
	{
		const bool ready = state.m_Readiness.IsReady();
		if (!ready || !m_LastFrameState || m_LastFrameState->m_SettleKey != state.m_SettleKey)
		{
			m_SettledFrames = 0;
		}
		m_FrameReady = ready;
		m_HasOpenFrame = true;

		if (!m_IsShuttingDown)
		{
			for (Entry& entry : m_Entries)
			{
				if (entry.m_Phase == Phase::Waiting && IsDue(entry, state, ready))
				{
					Issue(entry, state);
				}
			}
		}
		m_LastFrameState = std::move(state);
	}

	void FrameCaptureCoordinator::OnFrameSubmitted() noexcept
	{
		if (m_HasOpenFrame && m_FrameReady &&
			m_SettledFrames < std::numeric_limits<uint32_t>::max())
		{
			++m_SettledFrames;
		}
		m_HasOpenFrame = false;
	}

	void FrameCaptureCoordinator::Update() noexcept
	{
		if (m_Capture)
		{
			std::vector<FrameCaptureResult> results;
			m_Capture->ConsumeResults(results);
			for (FrameCaptureResult& result : results)
			{
				HandleCaptureResult(std::move(result));
			}
		}
		CollectEncodedJobs(false);
	}

	void FrameCaptureCoordinator::PrepareForShutdown() noexcept
	{
		m_IsShuttingDown = true;
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled,
					"The runtime shut down before the capture was due.");
			}
		}
		Update();
		CollectEncodedJobs(true);
	}

	void FrameCaptureCoordinator::FinalizeAfterRenderHost() noexcept
	{
		m_IsShuttingDown = true;
		// The task system has stopped; the final results are encoded inline.
		m_TaskSystem = nullptr;
		Update();
		CollectEncodedJobs(true);
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase == Phase::Waiting)
			{
				Finish(entry, FrameCaptureRequestStatus::Cancelled,
					"The runtime shut down before the capture was due.");
			}
			else if (entry.m_Phase == Phase::Issued)
			{
				Finish(entry, FrameCaptureRequestStatus::Failed,
					"The render host finalized without a capture result.");
			}
		}
		m_Capture = nullptr;
	}

	bool FrameCaptureCoordinator::IsDue(
		const Entry& entry, const FrameCaptureFrameState& state, bool ready) const noexcept
	{
		const FrameCaptureRequest& request = entry.m_Request;
		if (request.m_Timing == FrameCaptureTiming::NextFrame)
		{
			return true;
		}
		const bool contentMatches = request.m_RequiredContentId.empty() ||
			request.m_RequiredContentId == state.m_DemoId ||
			request.m_RequiredContentId == state.m_LabId;
		return ready && contentMatches && m_SettledFrames >= request.m_SettleFrames;
	}

	void FrameCaptureCoordinator::Issue(Entry& entry, const FrameCaptureFrameState& state) noexcept
	{
		if (!m_Capture)
		{
			Finish(entry, FrameCaptureRequestStatus::Failed, "Frame capture is unavailable.");
			return;
		}

		const FrameCaptureRequest& request = entry.m_Request;
		FrameCaptureMetadata& metadata = entry.m_Metadata;
		metadata.m_RequestId = entry.m_Id;
		metadata.m_Label = request.m_Label;
		metadata.m_Note = request.m_Note;
		metadata.m_Source = request.m_Source;
		metadata.m_Timing = request.m_Timing;
		metadata.m_SettleFrames =
			request.m_Timing == FrameCaptureTiming::AfterReady ? request.m_SettleFrames : 0;
		metadata.m_SettledFrames = m_SettledFrames;
		metadata.m_Backend = state.m_Backend;
		metadata.m_DemoId = state.m_DemoId;
		metadata.m_LabId = state.m_LabId;
		metadata.m_FrameIndex = state.m_FrameIndex;
		metadata.m_Camera = state.m_Camera;
		metadata.m_FixedDeltaTime = state.m_FixedDeltaTime;
		metadata.m_TotalTime = state.m_TotalTime;
		metadata.m_DevelopmentTools = state.m_DevelopmentTools;
		metadata.m_Readiness = state.m_Readiness;
		metadata.m_CapturedAtUtc = std::format("{:%FT%TZ}",
			std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now()));

		entry.m_CaptureRequestId = m_Capture->RequestCapture(request.m_Source);
		entry.m_Phase = Phase::Issued;
	}

	void FrameCaptureCoordinator::HandleCaptureResult(FrameCaptureResult result) noexcept
	{
		const auto entry = std::ranges::find_if(m_Entries, [&](const Entry& candidate)
			{
				return candidate.m_Phase == Phase::Issued &&
					candidate.m_CaptureRequestId == result.m_RequestId;
			});
		if (entry == m_Entries.end())
		{
			GGLAB_LOG_WARN("Frame capture result {} has no coordinator request.",
				result.m_RequestId);
			return;
		}

		entry->m_Metadata.m_FrameSerial = result.m_FrameSerial;
		if (result.m_Status != FrameCaptureStatus::Completed || !result.m_Image)
		{
			Finish(*entry, FrameCaptureRequestStatus::Failed,
				result.m_Failure.empty() ? std::string("The frame capture failed.")
				: std::move(result.m_Failure));
			return;
		}
		StartEncoding(*entry, result);
	}

	void FrameCaptureCoordinator::StartEncoding(
		Entry& entry, const FrameCaptureResult& result) noexcept
	{
		const std::filesystem::path directory = entry.m_Request.m_OutputDirectory.empty()
			? m_DefaultOutputDirectory
			: entry.m_Request.m_OutputDirectory;
		if (directory.empty())
		{
			Finish(entry, FrameCaptureRequestStatus::Failed,
				"No capture output directory is configured.");
			return;
		}

		FrameCaptureMetadata& metadata = entry.m_Metadata;
		const FrameCaptureImage& image = *result.m_Image;
		metadata.m_Width = image.m_Width;
		metadata.m_Height = image.m_Height;
		metadata.m_DisplayFormat = GetRHIFormatInfo(image.m_Format).m_Name;

		const std::string_view subject = !entry.m_Request.m_Label.empty()
			? std::string_view(entry.m_Request.m_Label)
			: !metadata.m_LabId.empty() ? std::string_view(metadata.m_LabId)
			: std::string_view(metadata.m_DemoId);
		const std::string timestamp = metadata.m_CapturedAtUtc.size() >= 19
			? std::format("{}{}{}-{}{}{}", metadata.m_CapturedAtUtc.substr(0, 4),
				metadata.m_CapturedAtUtc.substr(5, 2), metadata.m_CapturedAtUtc.substr(8, 2),
				metadata.m_CapturedAtUtc.substr(11, 2), metadata.m_CapturedAtUtc.substr(14, 2),
				metadata.m_CapturedAtUtc.substr(17, 2))
			: std::string("unknown-time");
		const std::string baseStem = std::format("{}-{}-{}-r{}", SanitizeFileStemComponent(subject),
			GetFrameCaptureSourceName(metadata.m_Source), timestamp, entry.m_Id);

		std::filesystem::path imagePath;
		std::filesystem::path metadataPath;
		for (uint32_t attempt = 1;; ++attempt)
		{
			const std::string stem =
				attempt == 1 ? baseStem : std::format("{}-{}", baseStem, attempt);
			imagePath = directory / (stem + ".png");
			metadataPath = directory / (stem + ".json");
			std::error_code errorCode;
			if (std::ranges::find(m_ReservedPaths, imagePath) == m_ReservedPaths.end() &&
				!std::filesystem::exists(imagePath, errorCode) &&
				!std::filesystem::exists(metadataPath, errorCode))
			{
				break;
			}
		}
		m_ReservedPaths.push_back(imagePath);
		metadata.m_ImageFile = imagePath.filename().string();

		auto job = std::make_shared<EncodeJob>();
		job->m_Image = result.m_Image;
		job->m_ImagePath = imagePath;
		job->m_MetadataPath = metadataPath;
		job->m_MetadataJson = SerializeFrameCaptureMetadata(metadata);
		entry.m_Job = job;
		entry.m_Phase = Phase::Encoding;

		const bool submitted = m_TaskSystem && m_TaskSystem->IsAcceptingTasks() &&
			m_TaskSystem->Submit(TaskDesc{ .m_Name = "FrameCapture.EncodePng",
				.m_Priority = TaskPriority::Normal },
				[job](std::stop_token) noexcept
				{
					RunEncodeJob(*job);
					return TaskResult::Success();
				}).IsValid();
		if (!submitted)
		{
			RunEncodeJob(*job);
		}
	}

	void FrameCaptureCoordinator::CollectEncodedJobs(bool wait) noexcept
	{
		for (Entry& entry : m_Entries)
		{
			if (entry.m_Phase != Phase::Encoding)
			{
				continue;
			}
			EncodeJob& job = *entry.m_Job;
			std::unique_lock lock(job.m_Mutex);
			if (wait)
			{
				job.m_Condition.wait_for(
					lock, EncodeShutdownTimeout, [&job]() { return job.m_IsDone; });
			}
			if (!job.m_IsDone)
			{
				if (wait)
				{
					lock.unlock();
					Finish(entry, FrameCaptureRequestStatus::Failed,
						"Encoding did not finish before shutdown.");
				}
				continue;
			}
			std::string failure = std::move(job.m_Failure);
			lock.unlock();
			Finish(entry,
				failure.empty() ? FrameCaptureRequestStatus::Completed
				: FrameCaptureRequestStatus::Failed,
				std::move(failure));
		}
	}

	void FrameCaptureCoordinator::Finish(
		Entry& entry, FrameCaptureRequestStatus status, std::string failure) noexcept
	{
		FrameCaptureRequestResult result{
			.m_RequestId = entry.m_Id,
			.m_Status = status,
			.m_Failure = std::move(failure),
		};
		if (entry.m_Job)
		{
			std::erase(m_ReservedPaths, entry.m_Job->m_ImagePath);
			if (status == FrameCaptureRequestStatus::Completed)
			{
				result.m_ImagePath = entry.m_Job->m_ImagePath;
				result.m_MetadataPath = entry.m_Job->m_MetadataPath;
			}
		}
		if (entry.m_Phase != Phase::Waiting)
		{
			result.m_Metadata = entry.m_Metadata;
		}
		m_Results.push_back(std::move(result));
		entry.m_Job.reset();
		entry.m_Phase = Phase::Finished;
	}

	void FrameCaptureCoordinator::RemoveFinishedEntries() noexcept
	{
		std::erase_if(m_Entries,
			[](const Entry& entry) { return entry.m_Phase == Phase::Finished; });
	}

	void FrameCaptureCoordinator::RunEncodeJob(EncodeJob& job) noexcept
	{
		std::string failure = EncodeAndWrite(
			job.m_Image, job.m_ImagePath, job.m_MetadataPath, job.m_MetadataJson);
		{
			std::scoped_lock lock(job.m_Mutex);
			job.m_Failure = std::move(failure);
			job.m_IsDone = true;
		}
		job.m_Condition.notify_all();
	}
}
