#pragma once
#include "Logger.h"

#if defined (BUILD_DEBUG)
#define GGLAB_LOG_INFO(...)					graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Application)->info(__VA_ARGS__)
#define GGLAB_LOG_WARN(...)					graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Application)->warn(__VA_ARGS__)
#define GGLAB_LOG_ERROR(...)				graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Application)->error(__VA_ARGS__)
#define GGLAB_LOG_CRITICAL(...)				graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Application)->critical(__VA_ARGS__)

#define GGLAB_LOG_GRAPHICS_INFO(...)		graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Graphics)->info(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_WARN(...)		graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Graphics)->warn(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_ERROR(...)		graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Graphics)->error(__VA_ARGS__)
#define GGLAB_LOG_GRAPHICS_CRITICAL(...)	graphicsGadgetLab::Logger::GetLogger(Logger::LoggerType::Graphics)->critical(__VA_ARGS__)

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

