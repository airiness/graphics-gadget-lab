#include "Core/Log/Logger.h"
#include "Core/CoreMacros.h"

#include <spdlog/sinks/stdout_color_sinks.h>

#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		[[nodiscard]] std::string_view LoggerName(Logger::LoggerType type) noexcept
		{
			return type == Logger::LoggerType::Application ? "APPLICATION" : "GRAPHICS";
		}

		std::shared_ptr<spdlog::logger> CreateDefaultLogger(Logger::LoggerType type)
		{
			auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
			consoleSink->set_pattern("[%T] [%^%l%$] [PID %P] [%n] %v");

			auto logger =
				std::make_shared<spdlog::logger>(std::string(LoggerName(type)), consoleSink);
			logger->set_level(spdlog::level::trace);
			spdlog::register_logger(logger);
			return logger;
		}
	}

	std::array<std::shared_ptr<spdlog::logger>, static_cast<uint32_t>(Logger::LoggerType::Count)>
		Logger::s_Loggers;

	std::array<std::once_flag, static_cast<uint32_t>(Logger::LoggerType::Count)>
		Logger::s_LoggerCreationFlags;

	void Logger::Initialize() noexcept
	{
		// Loggers are available without initialization; this only guarantees
		// they exist before the main loop starts.
		GGLAB_UNUSED(GetLogger(LoggerType::Application));
		GGLAB_UNUSED(GetLogger(LoggerType::Graphics));
	}

	const std::shared_ptr<spdlog::logger>& Logger::GetLogger(LoggerType type) noexcept
	{
		const uint32_t index = static_cast<uint32_t>(type);
		std::call_once(s_LoggerCreationFlags[index],
			[index, type]
			{
				s_Loggers[index] = CreateDefaultLogger(type);
			});

		return s_Loggers[index];
	}
}
