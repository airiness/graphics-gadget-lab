#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Task/TaskWorkerLifecycle.h"

namespace gglab::win32
{
	// Optional Windows worker policy: names worker threads and enters an MTA COM apartment.
	class Win32TaskWorkerLifecycle final : public TaskWorkerLifecycle
	{
	public:
		GGLAB_DELETE_COPYABLE_MOVABLE(Win32TaskWorkerLifecycle);
		Win32TaskWorkerLifecycle() noexcept = default;
		~Win32TaskWorkerLifecycle() override = default;

		[[nodiscard]] std::unique_ptr<TaskWorkerContext> CreateContext(
			uint32_t workerIndex) const override;
	};
}
