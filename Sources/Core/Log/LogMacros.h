#pragma once
#include "Core/Log/Logger.h"

#define GGLAB_LOG_APPLICATION_ALWAYS(level, ...) \
	gglab::Logger::GetLogger(gglab::Logger::LoggerType::Application)->log(level, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ALWAYS(level, ...) \
	gglab::Logger::GetLogger(gglab::Logger::LoggerType::Graphics)->log(level, __VA_ARGS__)

#define GGLAB_LOG_INFO_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(spdlog::level::info, __VA_ARGS__)
#define GGLAB_LOG_WARN_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(spdlog::level::warn, __VA_ARGS__)
#define GGLAB_LOG_ERROR_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(spdlog::level::err, __VA_ARGS__)
#define GGLAB_LOG_CRITICAL_ALWAYS(...) \
	GGLAB_LOG_APPLICATION_ALWAYS(spdlog::level::critical, __VA_ARGS__)

#define GGLAB_LOG_GRAPHICS_INFO_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(spdlog::level::info, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_WARN_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(spdlog::level::warn, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(spdlog::level::err, __VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_CRITICAL_ALWAYS(...) \
	GGLAB_LOG_GRAPHICS_ALWAYS(spdlog::level::critical, __VA_ARGS__)

#if defined(BUILD_DEBUG)
#define GGLAB_LOG_INFO(...)					GGLAB_LOG_INFO_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_WARN(...)					GGLAB_LOG_WARN_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_ERROR(...)				GGLAB_LOG_ERROR_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_CRITICAL(...)				GGLAB_LOG_CRITICAL_ALWAYS(__VA_ARGS__)

#define GGLAB_LOG_GRAPHICS_INFO(...)		GGLAB_LOG_GRAPHICS_INFO_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_WARN(...)		GGLAB_LOG_GRAPHICS_WARN_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ERROR(...)		GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_CRITICAL(...)	GGLAB_LOG_GRAPHICS_CRITICAL_ALWAYS(__VA_ARGS__)

#else
#define GGLAB_LOG_INFO(...)
#define GGLAB_LOG_WARN(...)
#define GGLAB_LOG_ERROR(...)
#define GGLAB_LOG_CRITICAL(...)

#define GGLAB_LOG_GRAPHICS_INFO(...)
#define GGLAB_LOG_GRAPHICS_WARN(...)
#define GGLAB_LOG_GRAPHICS_ERROR(...)
#define GGLAB_LOG_GRAPHICS_CRITICAL(...)
#endif
