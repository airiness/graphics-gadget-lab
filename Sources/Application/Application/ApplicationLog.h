#pragma once
#include "GGLabFoundation/Logging/Log.h"

namespace gglab::log_tags
{
	inline constexpr LogTag Application{ "APPLICATION" };
}

#define GGLAB_LOG_APPLICATION_ALWAYS(level, ...) \
	::gglab::Log(::gglab::log_tags::Application, level, __VA_ARGS__)

#define GGLAB_LOG_INFO_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(::gglab::LogLevel::Info, __VA_ARGS__)
#define GGLAB_LOG_WARN_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(::gglab::LogLevel::Warning, __VA_ARGS__)
#define GGLAB_LOG_ERROR_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(::gglab::LogLevel::Error, __VA_ARGS__)
#define GGLAB_LOG_CRITICAL_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(::gglab::LogLevel::Critical, __VA_ARGS__)

#if defined(BUILD_DEBUG)
#define GGLAB_LOG_INFO(...) GGLAB_LOG_INFO_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_WARN(...) GGLAB_LOG_WARN_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_ERROR(...) GGLAB_LOG_ERROR_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_CRITICAL(...) GGLAB_LOG_CRITICAL_ALWAYS(__VA_ARGS__)
#else
#define GGLAB_LOG_INFO(...)
#define GGLAB_LOG_WARN(...)
#define GGLAB_LOG_ERROR(...)
#define GGLAB_LOG_CRITICAL(...)
#endif
