#pragma once
#include <cstdint>
#include <array>
#include <memory>
#include <spdlog/spdlog.h>

namespace graphicsGadgetLab
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
		static void Initialize() noexcept;

		static std::shared_ptr<spdlog::logger>& GetLogger(LoggerType type) noexcept;

	private:
		static std::array<std::shared_ptr<spdlog::logger>, static_cast<uint32_t>(LoggerType::Count)> s_Loggers;
	};
}