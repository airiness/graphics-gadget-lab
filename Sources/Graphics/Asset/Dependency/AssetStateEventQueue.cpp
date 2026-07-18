#include "Core/Precompiled.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"

namespace gglab
{
	AssetStateEventQueue::AssetStateEventQueue() noexcept :
		m_OwnerThread(std::this_thread::get_id())
	{}

	uint64_t AssetStateEventQueue::Push(
		DependencyStatus status,
		std::optional<AssetOperationToken> operation,
		AssetStateEventOperationPhase operationPhase) noexcept
	{
		AssertOwnerThread();
		const bool hasOperation = operation.has_value();
		const bool hasOperationPhase =
			operationPhase != AssetStateEventOperationPhase::None;
		if (!status.IsValid() ||
			hasOperation != hasOperationPhase ||
			(operation && (!operation->IsValid() ||
				operation->m_ContentVersion != status.m_ContentVersion)))
		{
			GGLAB_ASSERT_MSG(false, "Invalid or mismatched asset state event.");
			return 0;
		}

		const uint64_t sequence = m_NextSequence++;
		m_PendingEvents.push_back({
			.m_Sequence = sequence,
			.m_Status = status,
			.m_Operation = operation,
			.m_OperationPhase = operationPhase,
		});
		return sequence;
	}

	void AssetStateEventQueue::Drain(std::vector<AssetStateEvent>& output) noexcept
	{
		AssertOwnerThread();
		output.clear();
		output.swap(m_PendingEvents);
		std::ranges::sort(
			output,
			{},
			&AssetStateEvent::m_Sequence);
	}

	void AssetStateEventQueue::AssertOwnerThread() const noexcept
	{
		GGLAB_ASSERT_MSG(
			std::this_thread::get_id() == m_OwnerThread,
			"AssetStateEventQueue accessed from a non-owner thread.");
	}
}
