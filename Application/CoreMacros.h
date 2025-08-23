#pragma once
#include <cassert>

#define GGLAB_ASSERT_MSG(expression, message) assert(expression && message)

// Define unreachable and abort application
#define GGLAB_UNREACHABLE(message) \
	do { \
		GGLAB_ASSERT_MSG(false, message); \
		std::abort(); \
	} while (0)

#define GGLAB_DELETE_COPYABLE(className) \
	className(const className&) noexcept = delete; \
	className& operator=(const className&) noexcept = delete;

#define GGLAB_DELETE_MOVABLE(className) \
	className(className&&) noexcept = delete;\
	className& operator=(className&&) noexcept = delete;

#define GGLAB_DELETE_COPYABLE_MOVABLE(className) \
	GGLAB_DELETE_COPYABLE(className) \
	GGLAB_DELETE_MOVABLE(className)

#define GGLAB_DEFAULT_COPYABLE(className) \
	className(const className&) noexcept = default; \
	clasaName& operator=(const className&) noexcept = default;

#define GGLAB_DEFAULT_MOVABLE(className) \
	className(className&&) noexcept = default; \
	className& operator=(className&&) noexcept = default;

#define GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(className) \
	GGLAB_DELETE_COPYABLE(className) \
	GGLAB_DEFAULT_MOVABLE(className)

#define GGLAB_INFINITE 0xFFFFFFFF
