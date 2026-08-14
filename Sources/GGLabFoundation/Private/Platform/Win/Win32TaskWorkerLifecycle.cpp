#include "GGLabFoundation/Platform/Win/Win32TaskWorkerLifecycle.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Logging/Log.h"

#include <Windows.h>
#include <objbase.h>

#include <format>
#include <memory>
#include <string>

namespace gglab::win32
{
	namespace
	{
		constexpr LogTag TaskLogTag{ "FOUNDATION_TASK" };

		class Win32TaskWorkerContext final : public TaskWorkerContext
		{
		public:
			explicit Win32TaskWorkerContext(uint32_t workerIndex) noexcept
			{
				const std::wstring threadName = std::format(L"gglab.TaskWorker.{}", workerIndex);
				GGLAB_UNUSED(::SetThreadDescription(::GetCurrentThread(), threadName.c_str()));

				const HRESULT comResult = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
				m_RequiresCleanup = SUCCEEDED(comResult);
				if (FAILED(comResult) && comResult != RPC_E_CHANGED_MODE)
				{
#if defined(BUILD_DEBUG)
					Log(TaskLogTag, LogLevel::Error,
						"TaskSystem worker {} failed to initialize COM: 0x{:08X}.", workerIndex,
						static_cast<uint32_t>(comResult));
#endif
				}
			}

			GGLAB_DELETE_COPYABLE_MOVABLE(Win32TaskWorkerContext);

			~Win32TaskWorkerContext() override
			{
				if (m_RequiresCleanup)
				{
					::CoUninitialize();
				}
			}

		private:
			bool m_RequiresCleanup = false;
		};
	}

	std::unique_ptr<TaskWorkerContext> Win32TaskWorkerLifecycle::CreateContext(
		uint32_t workerIndex) const
	{
		return std::make_unique<Win32TaskWorkerContext>(workerIndex);
	}
}
