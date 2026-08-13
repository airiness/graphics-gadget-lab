#include "Core/Task/TaskWorkerPlatformContext.h"
#include "Core/CoreMacros.h"
#include "Core/Log/LogMacros.h"

#include <Windows.h>
#include <objbase.h>

#include <format>
#include <string>

namespace gglab
{
	TaskWorkerPlatformContext::TaskWorkerPlatformContext(uint32_t workerIndex) noexcept
	{
		const std::wstring threadName = std::format(L"gglab.TaskWorker.{}", workerIndex);
		GGLAB_UNUSED(::SetThreadDescription(::GetCurrentThread(), threadName.c_str()));

		const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		m_RequiresCleanup = SUCCEEDED(comResult);
		if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
		{
			GGLAB_LOG_ERROR("TaskSystem worker {} failed to initialize COM: 0x{:08X}.", workerIndex,
				static_cast<uint32_t>(comResult));
		}
	}

	TaskWorkerPlatformContext::~TaskWorkerPlatformContext() noexcept
	{
		if (m_RequiresCleanup)
		{
			::CoUninitialize();
		}
	}
}
