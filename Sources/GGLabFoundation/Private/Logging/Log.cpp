#include "GGLabFoundation/Logging/Log.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace gglab
{
	namespace
	{
		[[nodiscard]] spdlog::level::level_enum ToSpdlogLevel(LogLevel level) noexcept
		{
			switch (level)
			{
			case LogLevel::Trace:
				return spdlog::level::trace;
			case LogLevel::Debug:
				return spdlog::level::debug;
			case LogLevel::Info:
				return spdlog::level::info;
			case LogLevel::Warning:
				return spdlog::level::warn;
			case LogLevel::Error:
				return spdlog::level::err;
			case LogLevel::Critical:
				return spdlog::level::critical;
			}
			return spdlog::level::info;
		}

		class DefaultLogSink final : public LogSink
		{
		public:
			void Write(LogTag tag, LogLevel level, std::string_view message) noexcept override
			{
				try
				{
					GetLogger(tag)->log(ToSpdlogLevel(level),
						spdlog::string_view_t(message.data(), message.size()));
				}
				catch (...)
				{
				}
			}

			void Flush() noexcept override
			{
				try
				{
					std::scoped_lock lock(m_Mutex);
					for (const auto& [name, logger] : m_Loggers)
					{
						logger->flush();
					}
				}
				catch (...)
				{
				}
			}

		private:
			[[nodiscard]] std::shared_ptr<spdlog::logger> GetLogger(LogTag tag)
			{
				const std::string name(tag.Name().empty() ? "GENERAL" : tag.Name());
				std::scoped_lock lock(m_Mutex);
				if (const auto iterator = m_Loggers.find(name); iterator != m_Loggers.end())
				{
					return iterator->second;
				}

				auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
				consoleSink->set_pattern("[%T] [%^%l%$] [PID %P] [%n] %v");
				auto logger = std::make_shared<spdlog::logger>(name, std::move(consoleSink));
				logger->set_level(spdlog::level::trace);
				m_Loggers.emplace(name, logger);
				return logger;
			}

			std::mutex m_Mutex;
			std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> m_Loggers;
		};

		std::mutex LogSinkMutex;
		std::shared_ptr<LogSink> ActiveLogSink;

		[[nodiscard]] const std::shared_ptr<LogSink>& GetDefaultLogSink()
		{
			static const std::shared_ptr<LogSink> Sink = std::make_shared<DefaultLogSink>();
			return Sink;
		}
	}

	void InitializeLogging() noexcept
	{
		GGLAB_UNUSED(GetLogSink());
	}

	bool IsLoggingInitialized() noexcept
	{
		std::scoped_lock lock(LogSinkMutex);
		return ActiveLogSink != nullptr;
	}

	std::shared_ptr<LogSink> GetLogSink() noexcept
	{
		std::scoped_lock lock(LogSinkMutex);
		if (!ActiveLogSink)
		{
			ActiveLogSink = GetDefaultLogSink();
		}
		return ActiveLogSink;
	}

	void SetLogSink(std::shared_ptr<LogSink> sink) noexcept
	{
		std::scoped_lock lock(LogSinkMutex);
		ActiveLogSink = sink ? std::move(sink) : GetDefaultLogSink();
	}

	void WriteLog(LogTag tag, LogLevel level, std::string_view message) noexcept
	{
		if (const std::shared_ptr<LogSink> sink = GetLogSink())
		{
			sink->Write(tag, level, message);
		}
	}

	void FlushLogs() noexcept
	{
		if (const std::shared_ptr<LogSink> sink = GetLogSink())
		{
			sink->Flush();
		}
	}
}
