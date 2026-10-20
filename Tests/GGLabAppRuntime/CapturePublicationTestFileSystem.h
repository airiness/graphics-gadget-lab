#pragma once
#include "Capture/CapturePublisher.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <system_error>

namespace gglab
{
	// Real temporary files and publication, with deterministic faults at the I/O
	// boundary. Configuration is fixed before the worker begins using this object.
	class CapturePublicationTestFileSystem final : public CapturePublicationFileSystemBase
	{
	public:
		enum class Block { None, TemporaryWrite, Publication };
		Block m_Block = Block::None;
		uint32_t m_FailWrite = 0;
		uint32_t m_FailPublish = 0;
		bool m_FailWithdraw = false;
		bool m_RenameOnly = false;

		std::string Write(const std::filesystem::path& path, std::span<const uint8_t> bytes,
			std::span<const uint8_t> tail) noexcept override
		{
			const auto failure = m_Native->Write(path, bytes, tail);
			if (++m_Writes == 1 && m_Block == Block::TemporaryWrite)
			{
				WaitForRelease();
			}
			return m_Writes == m_FailWrite ? "Injected temporary file write failure." : failure;
		}

		CaptureFilePublishOutcome Publish(const std::filesystem::path& temporary,
			const std::filesystem::path& final) noexcept override
		{
			if (++m_Publishes == 1 && m_Block == Block::Publication)
			{
				WaitForRelease();
			}
			if (m_Publishes == m_FailPublish)
			{
				return CaptureFilePublishOutcome::Failed;
			}
			if (m_RenameOnly)
			{
				std::error_code error;
				if (std::filesystem::exists(final, error))
				{
					return CaptureFilePublishOutcome::NameTaken;
				}
				std::filesystem::rename(temporary, final, error);
				return error ? CaptureFilePublishOutcome::Failed : CaptureFilePublishOutcome::Published;
			}
			return m_Native->Publish(temporary, final);
		}

		bool Withdraw(const std::filesystem::path& temporary,
			const std::filesystem::path& final) noexcept override
		{
			return !m_FailWithdraw && m_Native->Withdraw(temporary, final);
		}

		void Remove(const std::filesystem::path& path) noexcept override
		{
			m_Native->Remove(path);
		}

		[[nodiscard]] bool WaitEntered() noexcept
		{
			std::unique_lock lock(m_Mutex);
			return m_Condition.wait_for(lock, std::chrono::seconds(10),
				[this]() { return m_Entered; });
		}

		void Release() noexcept
		{
			{
				std::scoped_lock lock(m_Mutex);
				m_Released = true;
			}
			m_Condition.notify_all();
		}

	private:
		void WaitForRelease() noexcept
		{
			std::unique_lock lock(m_Mutex);
			m_Entered = true;
			m_Condition.notify_all();
			m_Condition.wait(lock, [this]() { return m_Released; });
		}

		std::shared_ptr<CapturePublicationFileSystemBase> m_Native = CreateCapturePublicationFileSystem();
		uint32_t m_Writes = 0;
		uint32_t m_Publishes = 0;
		std::mutex m_Mutex;
		std::condition_variable m_Condition;
		bool m_Entered = false;
		bool m_Released = false;
	};
}
