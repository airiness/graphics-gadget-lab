#pragma once
#include "GGLabFoundation/Task/TaskTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetDependencyTypes.h"
#include "GGLabRuntime/Graphics/Asset/AssetIdentity.h"
#include "GGLabRuntime/Graphics/Asset/AssetResidencyTypes.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gglab
{
	// Owner-thread residency state: interest leases and publication retains, the
	// model dependency graph and its ownership records, the residency policy
	// controller and the pending eviction/retirement queues. AssetManager keeps
	// the cross-subsystem orchestration that consumes this state.
	class AssetResidencyCoordinator final
	{
	public:
		struct PendingEviction
		{
			AssetResidencyOperation m_Operation{};
			uint64_t m_ResidentBytes = 0;
			uint64_t m_QuiescedFrame = 0;
		};

		struct PendingRetirement
		{
			AssetContentVersion m_ContentVersion{};
			uint64_t m_QueuedFrame = 0;
		};

		void AdvanceUsageFrame() noexcept { ++m_UsageFrame; }
		[[nodiscard]] uint64_t GetUsageFrame() const noexcept { return m_UsageFrame; }

		AssetOwnerId RegisterOwner() noexcept;
		void UnregisterOwner(AssetOwnerId owner) noexcept;
		[[nodiscard]] AssetLeaseAcquireResult AcquireLease(AssetOwnerId owner,
			AssetContentVersion contentVersion, TaskPriority priority) noexcept;
		[[nodiscard]] std::optional<AssetInterestChange> ReleaseLease(uint64_t leaseToken) noexcept;
		[[nodiscard]] std::optional<AssetInterestChange> UpdateLeasePriority(
			uint64_t leaseToken, TaskPriority priority) noexcept;

		[[nodiscard]] bool AcquirePublicationRetain(AssetContentVersion contentVersion) noexcept;
		void ReleasePublicationRetain(AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool HasPublicationRetain(AssetContentVersion contentVersion) const noexcept;

		[[nodiscard]] bool HasActiveInterest(AssetKey key) const noexcept;
		[[nodiscard]] TaskPriority GetEffectivePriority(
			AssetKey key, TaskPriority fallback) const noexcept;
		[[nodiscard]] AssetInterestTrackerStatistics GetInterestStatistics() const;

		[[nodiscard]] bool HasOwners() const noexcept;
		[[nodiscard]] bool HasLeases() const noexcept;
		[[nodiscard]] bool HasInterests() const noexcept;
		[[nodiscard]] bool HasPublicationRetains() const noexcept;

		[[nodiscard]] bool HasModelDependencyLeases(ModelID modelId) const noexcept;
		[[nodiscard]] bool HasModelDependencyOwner(ModelID modelId) const noexcept;
		[[nodiscard]] bool HasModelDependencyOwnership() const noexcept;
		[[nodiscard]] AssetOwnerId GetModelDependencyOwner(ModelID modelId) const noexcept;
		void SetModelDependencyOwner(ModelID modelId, AssetOwnerId owner) noexcept;
		void SetModelDependencyLeases(ModelID modelId, std::vector<uint64_t> leases) noexcept;
		void RecordModelDependencyLease(ModelID modelId, uint64_t leaseToken) noexcept;
		[[nodiscard]] std::vector<uint64_t> TakeModelDependencyLeases(ModelID modelId) noexcept;
		[[nodiscard]] std::optional<AssetOwnerId> TakeModelDependencyOwner(ModelID modelId) noexcept;
		template <typename Fn> void ForEachModelDependencyOwner(Fn&& fn) const
		{
			for (const auto& entry : m_ModelDependencyOwners)
			{
				std::invoke(fn, entry.first, entry.second);
			}
		}
		template <typename Fn> void ForEachTrackedModel(Fn&& fn) const
		{
			for (const auto& entry : m_ModelDependencyOwners)
			{
				std::invoke(fn, entry.first);
			}
			for (const auto& entry : m_ModelDependencyLeaseTokens)
			{
				std::invoke(fn, entry.first);
			}
		}
		template <typename Fn> void ForEachModelDependencyLease(ModelID modelId, Fn&& fn) const
		{
			const auto leases = m_ModelDependencyLeaseTokens.find(modelId);
			if (leases == m_ModelDependencyLeaseTokens.end())
			{
				return;
			}
			for (uint64_t leaseToken : leases->second)
			{
				std::invoke(fn, leaseToken);
			}
		}
		template <typename ModelPredicate> [[nodiscard]] bool IsDependencyOwnerProtecting(
			AssetOwnerId owner, ModelPredicate&& isProtecting) const
		{
			for (const auto& entry : m_ModelDependencyOwners)
			{
				if (entry.second == owner)
				{
					return std::invoke(isProtecting, entry.first);
				}
			}
			return true;
		}
		[[nodiscard]] bool HasActiveInterestMatchingOwner(AssetKey key,
			const std::function<bool(AssetOwnerId)>& predicate) const
		{
			return m_InterestTracker.HasActiveInterestMatchingOwner(key, predicate);
		}

		void RegisterModel(AssetContentVersion modelVersion,
			std::span<const DependencyStatus> dependencies,
			uint32_t structuralFailureCount) noexcept;
		void UnregisterModel(AssetContentVersion modelVersion) noexcept;
		void ApplyStatus(const DependencyStatus& status,
			std::vector<AssetDependencyChange>& changes) noexcept;
		[[nodiscard]] const AssetDependencyModelState* FindModel(
			AssetContentVersion modelVersion) const noexcept;
		[[nodiscard]] std::span<const AssetContentVersion> FindDependents(
			AssetContentVersion dependency) const noexcept;
		[[nodiscard]] ModelDependencyOutcome EvaluateModel(
			AssetContentVersion modelVersion) const noexcept;
		[[nodiscard]] AssetDependencyGraphStatistics GetDependencyStatistics() const noexcept;
		void RecordDependencyValidation(bool matched) noexcept;
		[[nodiscard]] uint64_t GetDependencyValidationCount() const noexcept;
		[[nodiscard]] uint64_t GetDependencyValidationMismatchCount() const noexcept;

		[[nodiscard]] std::vector<PendingEviction>& PendingEvictions() noexcept;
		[[nodiscard]] const std::vector<PendingEviction>& PendingEvictions() const noexcept;
		[[nodiscard]] std::vector<PendingRetirement>& PendingRetirements() noexcept;
		[[nodiscard]] const std::vector<PendingRetirement>& PendingRetirements() const noexcept;
		void AddLogicalResidentBytes(uint64_t bytes) noexcept;
		void SubtractLogicalResidentBytes(uint64_t bytes) noexcept;
		void SetLogicalResidentBytes(uint64_t bytes) noexcept { m_LogicalResidentBytes = bytes; }
		[[nodiscard]] uint64_t GetLogicalResidentBytes() const noexcept;

		[[nodiscard]] AssetResidencyController& Controller() noexcept { return m_ResidencyController; }
		[[nodiscard]] const AssetResidencyController& Controller() const noexcept
		{
			return m_ResidencyController;
		}

	private:
		AssetInterestTracker m_InterestTracker;
		AssetDependencyGraph m_DependencyGraph;
		AssetResidencyController m_ResidencyController;
		std::unordered_map<ModelID, AssetOwnerId> m_ModelDependencyOwners;
		std::unordered_map<ModelID, std::vector<uint64_t>> m_ModelDependencyLeaseTokens;
		std::vector<PendingEviction> m_PendingEvictions;
		std::vector<PendingRetirement> m_PendingRetirements;
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_UsageFrame = 0;
		uint64_t m_DependencyValidationCount = 0;
		uint64_t m_DependencyValidationMismatchCount = 0;
	};
}
