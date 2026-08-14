#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"

#include <cstdint>

namespace gglab
{
	class TaskWorkerPlatformContext final
	{
	public:
		explicit TaskWorkerPlatformContext(uint32_t workerIndex) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TaskWorkerPlatformContext);
		~TaskWorkerPlatformContext() noexcept;

	private:
		bool m_RequiresCleanup = false;
	};
}
