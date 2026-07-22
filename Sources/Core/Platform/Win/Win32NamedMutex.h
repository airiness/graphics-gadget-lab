#pragma once
#include "Core/CoreMacros.h"

#include <cstdint>
#include <limits>
#include <string_view>

namespace gglab::win32
{
	enum class NamedMutexAcquireDisposition : uint8_t
	{
		Failed,
		TimedOut,
		Acquired,
		Abandoned,
	};

	class NamedMutexGuard final
	{
	public:
		NamedMutexGuard() noexcept = default;
		NamedMutexGuard(NamedMutexGuard&& other) noexcept;
		NamedMutexGuard& operator=(NamedMutexGuard&& other) noexcept;
		GGLAB_DELETE_COPYABLE(NamedMutexGuard);
		~NamedMutexGuard();

		[[nodiscard]] bool IsAcquired() const noexcept
		{
			return m_Disposition == NamedMutexAcquireDisposition::Acquired ||
				m_Disposition == NamedMutexAcquireDisposition::Abandoned;
		}
		[[nodiscard]] bool WasAbandoned() const noexcept
		{
			return m_Disposition == NamedMutexAcquireDisposition::Abandoned;
		}
		[[nodiscard]] NamedMutexAcquireDisposition GetDisposition() const noexcept
		{
			return m_Disposition;
		}

	private:
		friend class NamedMutex;
		NamedMutexGuard(void* handle, NamedMutexAcquireDisposition disposition) noexcept;
		void Release() noexcept;

		void* m_Handle = nullptr;
		NamedMutexAcquireDisposition m_Disposition =
			NamedMutexAcquireDisposition::Failed;
	};

	class NamedMutex final
	{
	public:
		explicit NamedMutex(std::wstring_view name) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(NamedMutex);
		~NamedMutex();

		[[nodiscard]] bool IsValid() const noexcept { return m_Handle != nullptr; }
		[[nodiscard]] NamedMutexGuard Acquire(
			uint32_t timeoutMilliseconds = std::numeric_limits<uint32_t>::max()) const noexcept;

	private:
		void* m_Handle = nullptr;
	};
}
