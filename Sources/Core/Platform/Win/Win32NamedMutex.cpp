#include "Core/Precompiled.h"
#include "Core/Platform/Win/Win32NamedMutex.h"

namespace gglab::win32
{
	NamedMutexGuard::NamedMutexGuard(
		void* handle,
		NamedMutexAcquireDisposition disposition) noexcept :
		m_Handle(handle),
		m_Disposition(disposition)
	{
	}

	NamedMutexGuard::NamedMutexGuard(NamedMutexGuard&& other) noexcept :
		m_Handle(std::exchange(other.m_Handle, nullptr)),
		m_Disposition(std::exchange(
			other.m_Disposition,
			NamedMutexAcquireDisposition::Failed))
	{
	}

	NamedMutexGuard& NamedMutexGuard::operator=(NamedMutexGuard&& other) noexcept
	{
		if (this == &other) return *this;
		Release();
		m_Handle = std::exchange(other.m_Handle, nullptr);
		m_Disposition = std::exchange(
			other.m_Disposition,
			NamedMutexAcquireDisposition::Failed);
		return *this;
	}

	NamedMutexGuard::~NamedMutexGuard()
	{
		Release();
	}

	void NamedMutexGuard::Release() noexcept
	{
		if (IsAcquired() && m_Handle)
		{
			GGLAB_UNUSED(::ReleaseMutex(static_cast<HANDLE>(m_Handle)));
		}
		m_Handle = nullptr;
		m_Disposition = NamedMutexAcquireDisposition::Failed;
	}

	NamedMutex::NamedMutex(std::wstring_view name) noexcept
	{
		if (name.empty()) return;
		const std::wstring nullTerminatedName(name);
		m_Handle = ::CreateMutexW(nullptr, FALSE, nullTerminatedName.c_str());
	}

	NamedMutex::~NamedMutex()
	{
		if (m_Handle)
		{
			::CloseHandle(static_cast<HANDLE>(m_Handle));
		}
	}

	NamedMutexGuard NamedMutex::Acquire(uint32_t timeoutMilliseconds) const noexcept
	{
		if (!m_Handle) return {};
		const DWORD timeout = timeoutMilliseconds == std::numeric_limits<uint32_t>::max() ?
			INFINITE : timeoutMilliseconds;
		const DWORD result = ::WaitForSingleObject(static_cast<HANDLE>(m_Handle), timeout);
		switch (result)
		{
		case WAIT_OBJECT_0:
			return { m_Handle, NamedMutexAcquireDisposition::Acquired };
		case WAIT_ABANDONED:
			return { m_Handle, NamedMutexAcquireDisposition::Abandoned };
		case WAIT_TIMEOUT:
			return { nullptr, NamedMutexAcquireDisposition::TimedOut };
		default:
			return {};
		}
	}
}
