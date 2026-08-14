#pragma once

#include <cstdint>
#include <format>
#include <memory>
#include <string_view>

namespace gglab
{
	class LogTag final
	{
	public:
		constexpr explicit LogTag(std::string_view name) noexcept : m_Name(name) {}

		[[nodiscard]] constexpr std::string_view Name() const noexcept { return m_Name; }

	private:
		std::string_view m_Name;
	};

	enum class LogLevel : std::uint8_t
	{
		Trace,
		Debug,
		Info,
		Warning,
		Error,
		Critical,
	};

	class LogSink
	{
	public:
		virtual ~LogSink() = default;
		virtual void Write(LogTag tag, LogLevel level, std::string_view message) noexcept = 0;
		virtual void Flush() noexcept {}
	};

	// Logging is also initialized lazily by the first write. Explicit initialization
	// lets a host establish the default sink before starting worker threads.
	void InitializeLogging() noexcept;
	[[nodiscard]] bool IsLoggingInitialized() noexcept;
	[[nodiscard]] std::shared_ptr<LogSink> GetLogSink() noexcept;
	void SetLogSink(std::shared_ptr<LogSink> sink) noexcept;
	void WriteLog(LogTag tag, LogLevel level, std::string_view message) noexcept;
	void FlushLogs() noexcept;

	inline void Log(LogTag tag, LogLevel level, std::string_view message) noexcept
	{
		WriteLog(tag, level, message);
	}

	template <typename... Args>
		requires(sizeof...(Args) > 0)
	void Log(LogTag tag, LogLevel level, std::string_view format, Args&&... args) noexcept
	{
		try
		{
			WriteLog(tag, level,
				std::vformat(format, std::make_format_args(args...)));
		}
		catch (...)
		{
			// Formatting failures must not turn diagnostics into a process failure.
			WriteLog(tag, level, format);
		}
	}
}
