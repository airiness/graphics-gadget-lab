#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <mutex>
#include <spdlog/spdlog.h>

namespace gglab
{
	class Logger
	{
	public:
		enum class LoggerType : uint32_t
		{
			Application,
			Graphics,

			Count
		};

	public:
		// Loggers are available out of the box: GetLogger creates the default
		// console logger lazily on first use, so logging works from the very
		// first statement of a command-line or headless process. Initialize
		// only guarantees they exist before the main loop starts and is
		// idempotent.
		static void Initialize() noexcept;

		// Never returns null. The returned logger is stable for the process
		// lifetime after the first call.
		static const std::shared_ptr<spdlog::logger>& GetLogger(LoggerType type) noexcept;

	private:
		static std::array<std::shared_ptr<spdlog::logger>, static_cast<uint32_t>(LoggerType::Count)>
			s_Loggers;
		static std::array<std::once_flag, static_cast<uint32_t>(LoggerType::Count)>
			s_LoggerCreationFlags;
	};
}
