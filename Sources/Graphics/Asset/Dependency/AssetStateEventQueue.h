#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"

#include <optional>
#include <thread>
#include <vector>

namespace gglab
{
	struct AssetStateEvent
	{
		uint64_t m_Sequence = 0;
		DependencyStatus m_Status{};
		std::optional<AssetOperationToken> m_Operation;
		AssetStateEventOperationPhase m_OperationPhase =
			AssetStateEventOperationPhase::None;
	};

	class AssetStateEventQueue final
	{
	public:
		AssetStateEventQueue() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetStateEventQueue);

		[[nodiscard]] uint64_t Push(
			DependencyStatus status,
			std::optional<AssetOperationToken> operation = std::nullopt,
			AssetStateEventOperationPhase operationPhase =
				AssetStateEventOperationPhase::None) noexcept;
		void Drain(std::vector<AssetStateEvent>& output) noexcept;

		[[nodiscard]] bool HasPendingEvents() const noexcept
		{
			return !m_PendingEvents.empty();
		}

	private:
		void AssertOwnerThread() const noexcept;

		std::thread::id m_OwnerThread;
		uint64_t m_NextSequence = 1;
		std::vector<AssetStateEvent> m_PendingEvents;
	};
}
