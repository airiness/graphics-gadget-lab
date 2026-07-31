#include "Core/Precompiled.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"

namespace gglab
{
	AssetStateEventQueue::AssetStateEventQueue() noexcept :
		m_OwnerThread(std::this_thread::get_id())
	{
	}

	void AssetStateEventQueue::Push(DependencyStatus status,
		std::optional<AssetOperationToken> operation,
		AssetStateEventOperationPhase operationPhase) noexcept
	{
		AssertOwnerThread();
		const bool hasOperation = operation.has_value();
		const bool hasOperationPhase = operationPhase != AssetStateEventOperationPhase::None;
		if (!status.IsValid() || hasOperation != hasOperationPhase ||
			(operation &&
				(!operation->IsValid() || operation->m_ContentVersion != status.m_ContentVersion)))
		{
			GGLAB_ASSERT_MSG(false, "Invalid or mismatched asset state event.");
			return;
		}

		m_PendingEvents.push_back({
			.m_Status = status,
			.m_Operation = operation,
			.m_OperationPhase = operationPhase,
			});
	}

	void AssetStateEventQueue::Drain(std::vector<AssetStateEvent>& output) noexcept
	{
		AssertOwnerThread();
		output.clear();
		output.swap(m_PendingEvents);
	}

	void AssetStateEventQueue::AssertOwnerThread() const noexcept
	{
		GGLAB_ASSERT_MSG(std::this_thread::get_id() == m_OwnerThread,
			"AssetStateEventQueue accessed from a non-owner thread.");
	}
}
