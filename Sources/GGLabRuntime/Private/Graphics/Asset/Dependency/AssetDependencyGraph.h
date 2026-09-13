#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/Asset/AssetDependencyTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetIdentity.h"
#include "GGLabRuntime/Graphics/GraphicsTypes.h"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace gglab
{
	struct AssetDependencyModelState
	{
		AssetContentVersion m_Model{};
		std::unordered_map<AssetContentVersion, DependencyStatus, AssetContentVersionHash>
			m_DependencyStates;
		uint32_t m_StructuralFailureCount = 0;
		uint32_t m_ReadyCount = 0;
		uint32_t m_PendingCount = 0;
		uint32_t m_FailedCount = 0;
		uint32_t m_CancelledCount = 0;
		uint64_t m_EventUpdateCount = 0;
	};

	struct AssetDependencyChange
	{
		AssetContentVersion m_Model{};
		ModelDependencyOutcome m_CurrentOutcome = ModelDependencyOutcome::Failed;
	};

	struct AssetDependencyGraphStatistics
	{
		uint32_t m_TrackedModelCount = 0;
		uint32_t m_ReverseDependencyCount = 0;
		uint32_t m_ReverseDependencyEdgeCount = 0;
		uint64_t m_GraphBuildCount = 0;
		uint64_t m_EventUpdateCount = 0;
		uint64_t m_IgnoredEventCount = 0;
	};

	class AssetDependencyGraph final
	{
	public:
		AssetDependencyGraph() = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetDependencyGraph);

		[[nodiscard]] bool RegisterModel(AssetContentVersion model,
			std::span<const DependencyStatus> dependencies,
			uint32_t structuralFailureCount) noexcept;
		void UnregisterModel(AssetContentVersion model) noexcept;

		void ApplyStatus(
			const DependencyStatus& status, std::vector<AssetDependencyChange>& changes) noexcept;

		[[nodiscard]] const AssetDependencyModelState* FindModel(
			AssetContentVersion model) const noexcept;
		[[nodiscard]] std::span<const AssetContentVersion> FindDependents(
			AssetContentVersion dependency) const noexcept;
		[[nodiscard]] ModelDependencyOutcome EvaluateModel(
			AssetContentVersion model) const noexcept;
		[[nodiscard]] AssetDependencyGraphStatistics GetStatistics() const noexcept;

	private:
		using ModelStateMap = std::unordered_map<AssetKey, AssetDependencyModelState, AssetKeyHash>;
		using ReverseDependencyMap = std::unordered_map<AssetContentVersion,
			std::vector<AssetContentVersion>, AssetContentVersionHash>;

		void UnregisterModel(AssetKey model) noexcept;
		static void IncrementCounter(
			AssetDependencyModelState& state, ModelDependencyOutcome outcome) noexcept;
		static void DecrementCounter(
			AssetDependencyModelState& state, ModelDependencyOutcome outcome) noexcept;
		[[nodiscard]] static ModelDependencyOutcome EvaluateModel(
			const AssetDependencyModelState& state) noexcept;

		ModelStateMap m_Models;
		ReverseDependencyMap m_ReverseDependencies;
		uint64_t m_GraphBuildCount = 0;
		uint64_t m_EventUpdateCount = 0;
		uint64_t m_IgnoredEventCount = 0;
	};
}
