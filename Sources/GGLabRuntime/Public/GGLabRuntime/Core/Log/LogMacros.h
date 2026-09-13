#pragma once
#include "GGLabFoundation/Logging/Log.h"

namespace gglab::log_tags
{
	inline constexpr LogTag Runtime{ "RUNTIME" };
	inline constexpr LogTag Graphics{ "GRAPHICS" };
}

#define GGLAB_LOG_RUNTIME_ALWAYS(level, ...) \
	::gglab::Log(::gglab::log_tags::Runtime, level, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ALWAYS(level, ...) \
	::gglab::Log(::gglab::log_tags::Graphics, level, __VA_ARGS__)

#define GGLAB_LOG_RUNTIME_INFO_ALWAYS(...) \
	GGLAB_LOG_RUNTIME_ALWAYS(::gglab::LogLevel::Info, __VA_ARGS__)
#define GGLAB_LOG_RUNTIME_WARN_ALWAYS(...) \
	GGLAB_LOG_RUNTIME_ALWAYS(::gglab::LogLevel::Warning, __VA_ARGS__)
#define GGLAB_LOG_RUNTIME_ERROR_ALWAYS(...) \
	GGLAB_LOG_RUNTIME_ALWAYS(::gglab::LogLevel::Error, __VA_ARGS__)
#define GGLAB_LOG_RUNTIME_CRITICAL_ALWAYS(...) \
	GGLAB_LOG_RUNTIME_ALWAYS(::gglab::LogLevel::Critical, __VA_ARGS__)

#define GGLAB_LOG_GRAPHICS_INFO_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(::gglab::LogLevel::Info, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_WARN_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(::gglab::LogLevel::Warning, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(::gglab::LogLevel::Error, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_CRITICAL_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(::gglab::LogLevel::Critical, __VA_ARGS__)

#if defined(BUILD_DEBUG)
#define GGLAB_LOG_RUNTIME_INFO(...) GGLAB_LOG_RUNTIME_INFO_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_RUNTIME_WARN(...) GGLAB_LOG_RUNTIME_WARN_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_RUNTIME_ERROR(...) GGLAB_LOG_RUNTIME_ERROR_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_RUNTIME_CRITICAL(...) GGLAB_LOG_RUNTIME_CRITICAL_ALWAYS(__VA_ARGS__)

#define GGLAB_LOG_GRAPHICS_INFO(...) GGLAB_LOG_GRAPHICS_INFO_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_WARN(...) GGLAB_LOG_GRAPHICS_WARN_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ERROR(...) GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_CRITICAL(...) GGLAB_LOG_GRAPHICS_CRITICAL_ALWAYS(__VA_ARGS__)

#else
#define GGLAB_LOG_RUNTIME_INFO(...)
#define GGLAB_LOG_RUNTIME_WARN(...)
#define GGLAB_LOG_RUNTIME_ERROR(...)
#define GGLAB_LOG_RUNTIME_CRITICAL(...)

#define GGLAB_LOG_GRAPHICS_INFO(...)
#define GGLAB_LOG_GRAPHICS_WARN(...)
#define GGLAB_LOG_GRAPHICS_ERROR(...)
#define GGLAB_LOG_GRAPHICS_CRITICAL(...)
#endif
