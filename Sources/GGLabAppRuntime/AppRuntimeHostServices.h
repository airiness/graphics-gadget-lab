#pragma once

#include "Capture/FrameCaptureImageEncoder.h"
#include "GGLabFoundation/Task/TaskWorkerLifecycle.h"

#include <memory>

namespace gglab
{
	struct AppRuntimeHostServices
	{
		// Optional by contract: TaskSystem retains its portable no-op policy when absent.
		std::shared_ptr<const TaskWorkerLifecycle> m_TaskWorkerLifecycle;
		// Encodes frame capture PNGs with the host's image codec. Without it frame
		// captures fail when they are written.
		FrameCaptureImageEncoder m_FrameCaptureImageEncoder;
	};
}
