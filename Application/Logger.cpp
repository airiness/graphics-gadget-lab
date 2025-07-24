#include "Logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

namespace graphicsGadgetLab
{
	std::array<std::shared_ptr<spdlog::logger>, static_cast<uint32_t>(Logger::LoggerType::Count)> Logger::s_Loggers;

	void Logger::Initialize() noexcept
	{
		auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		console_sink->set_pattern("[%T] [%^%l%$] [PID %P] [%n] %v");

		s_Loggers[static_cast<uint32_t>(LoggerType::Application)] = std::make_shared<spdlog::logger>("APPLICATION", console_sink);
		s_Loggers[static_cast<uint32_t>(LoggerType::Application)]->set_level(spdlog::level::trace);
		spdlog::register_logger(s_Loggers[static_cast<uint32_t>(LoggerType::Application)]);

		s_Loggers[static_cast<uint32_t>(LoggerType::Graphics)] = std::make_shared<spdlog::logger>("GRAPHICS", console_sink);
		s_Loggers[static_cast<uint32_t>(LoggerType::Graphics)]->set_level(spdlog::level::trace);
		spdlog::register_logger(s_Loggers[static_cast<uint32_t>(LoggerType::Graphics)]);
	}

	std::shared_ptr<spdlog::logger>& Logger::GetLogger(LoggerType type) noexcept
	{
		return s_Loggers[static_cast<uint32_t>(type)];
	}
}