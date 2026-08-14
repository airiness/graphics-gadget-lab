#pragma once

#include <cstdint>
#include <memory>

namespace gglab
{
	// Owns platform/host state for one worker. Destruction happens on the same
	// worker thread after the scheduling loop exits.
	class TaskWorkerContext
	{
	public:
		virtual ~TaskWorkerContext() = default;
	};

	// Optional host policy. TaskSystem remains fully functional when this is absent.
	// CreateContext may be called concurrently by multiple worker threads.
	class TaskWorkerLifecycle
	{
	public:
		virtual ~TaskWorkerLifecycle() = default;
		[[nodiscard]] virtual std::unique_ptr<TaskWorkerContext> CreateContext(
			uint32_t workerIndex) const = 0;
	};
}
