#pragma once

namespace gglab
{
	class TaskSystem;
	struct TaskSystemSnapshot;

	void BuildTaskSystemSnapshot(
		const TaskSystem& taskSystem, TaskSystemSnapshot& outSnapshot) noexcept;
}
