#pragma once

#include "GGLabFoundation/Task/TaskWorkerLifecycle.h"

#include <memory>

namespace gglab
{
	struct AppRuntimeHostServices
	{
		// Optional by contract: TaskSystem retains its portable no-op policy when absent.
		std::shared_ptr<const TaskWorkerLifecycle> m_TaskWorkerLifecycle;
	};
}
