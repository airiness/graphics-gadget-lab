#include "Graphics/Asset/Residency/AssetResidencyCoordinator.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <algorithm>

namespace gglab
{
	AssetOwnerId AssetResidencyCoordinator::RegisterOwner() noexcept
	{
		return m_InterestTracker.RegisterOwner();
	}

	void AssetResidencyCoordinator::UnregisterOwner(AssetOwnerId owner) noexcept
	{
		m_InterestTracker.UnregisterOwner(owner);
	}

	AssetLeaseAcquireResult AssetResidencyCoordinator::AcquireLease(AssetOwnerId owner,
		AssetContentVersion contentVersion, TaskPriority priority) noexcept
	{
		return m_InterestTracker.AcquireLease(owner, contentVersion, priority);
	}

	std::optional<AssetInterestChange> AssetResidencyCoordinator::ReleaseLease(
		uint64_t leaseToken) noexcept
	{
		return m_InterestTracker.ReleaseLease(leaseToken);
	}

	std::optional<AssetInterestChange> AssetResidencyCoordinator::UpdateLeasePriority(
		uint64_t leaseToken, TaskPriority priority) noexcept
	{
		return m_InterestTracker.UpdateLeasePriority(leaseToken, priority);
	}

	bool AssetResidencyCoordinator::AcquirePublicationRetain(
		AssetContentVersion contentVersion) noexcept
	{
		return m_InterestTracker.AcquirePublicationRetain(contentVersion);
	}

	void AssetResidencyCoordinator::ReleasePublicationRetain(
		AssetContentVersion contentVersion) noexcept
	{
		m_InterestTracker.ReleasePublicationRetain(contentVersion);
	}

	bool AssetResidencyCoordinator::HasPublicationRetain(
		AssetContentVersion contentVersion) const noexcept
	{
		return m_InterestTracker.HasPublicationRetain(contentVersion);
	}

	bool AssetResidencyCoordinator::HasActiveInterest(AssetKey key) const noexcept
	{
		return m_InterestTracker.HasActiveInterest(key);
	}

	TaskPriority AssetResidencyCoordinator::GetEffectivePriority(
		AssetKey key, TaskPriority fallback) const noexcept
	{
		return m_InterestTracker.GetEffectivePriority(key, fallback);
	}

	AssetInterestTrackerStatistics AssetResidencyCoordinator::GetInterestStatistics() const
	{
		return m_InterestTracker.GetStatistics();
	}

	bool AssetResidencyCoordinator::HasOwners() const noexcept
	{
		return m_InterestTracker.HasOwners();
	}

	bool AssetResidencyCoordinator::HasLeases() const noexcept
	{
		return m_InterestTracker.HasLeases();
	}

	bool AssetResidencyCoordinator::HasInterests() const noexcept
	{
		return m_InterestTracker.HasInterests();
	}

	bool AssetResidencyCoordinator::HasPublicationRetains() const noexcept
	{
		return m_InterestTracker.HasPublicationRetains();
	}

	bool AssetResidencyCoordinator::HasModelDependencyLeases(ModelID modelId) const noexcept
	{
		return m_ModelDependencyLeaseTokens.contains(modelId);
	}

	bool AssetResidencyCoordinator::HasModelDependencyOwner(ModelID modelId) const noexcept
	{
		return m_ModelDependencyOwners.contains(modelId);
	}

	bool AssetResidencyCoordinator::HasModelDependencyOwnership() const noexcept
	{
		return !m_ModelDependencyOwners.empty() || !m_ModelDependencyLeaseTokens.empty();
	}

	AssetOwnerId AssetResidencyCoordinator::GetModelDependencyOwner(ModelID modelId) const noexcept
	{
		const auto found = m_ModelDependencyOwners.find(modelId);
		return found != m_ModelDependencyOwners.end() ? found->second : AssetOwnerId{};
	}

	void AssetResidencyCoordinator::SetModelDependencyOwner(
		ModelID modelId, AssetOwnerId owner) noexcept
	{
		m_ModelDependencyOwners.emplace(modelId, owner);
	}

	void AssetResidencyCoordinator::SetModelDependencyLeases(
		ModelID modelId, std::vector<uint64_t> leases) noexcept
	{
		m_ModelDependencyLeaseTokens[modelId] = std::move(leases);
	}

	void AssetResidencyCoordinator::RecordModelDependencyLease(
		ModelID modelId, uint64_t leaseToken) noexcept
	{
		m_ModelDependencyLeaseTokens[modelId].push_back(leaseToken);
	}

	std::vector<uint64_t> AssetResidencyCoordinator::TakeModelDependencyLeases(
		ModelID modelId) noexcept
	{
		std::vector<uint64_t> tokens;
		const auto leases = m_ModelDependencyLeaseTokens.find(modelId);
		if (leases != m_ModelDependencyLeaseTokens.end())
		{
			tokens = std::move(leases->second);
			m_ModelDependencyLeaseTokens.erase(leases);
		}
		return tokens;
	}

	std::optional<AssetOwnerId> AssetResidencyCoordinator::TakeModelDependencyOwner(
		ModelID modelId) noexcept
	{
		const auto owner = m_ModelDependencyOwners.find(modelId);
		if (owner == m_ModelDependencyOwners.end())
		{
			return std::nullopt;
		}
		const AssetOwnerId result = owner->second;
		m_ModelDependencyOwners.erase(owner);
		return result;
	}

	void AssetResidencyCoordinator::RegisterModel(AssetContentVersion modelVersion,
		std::span<const DependencyStatus> dependencies, uint32_t structuralFailureCount) noexcept
	{
		GGLAB_UNUSED(m_DependencyGraph.RegisterModel(
			modelVersion, dependencies, structuralFailureCount));
	}

	void AssetResidencyCoordinator::UnregisterModel(AssetContentVersion modelVersion) noexcept
	{
		m_DependencyGraph.UnregisterModel(modelVersion);
	}

	void AssetResidencyCoordinator::ApplyStatus(const DependencyStatus& status,
		std::vector<AssetDependencyChange>& changes) noexcept
	{
		m_DependencyGraph.ApplyStatus(status, changes);
	}

	const AssetDependencyModelState* AssetResidencyCoordinator::FindModel(
		AssetContentVersion modelVersion) const noexcept
	{
		return m_DependencyGraph.FindModel(modelVersion);
	}

	std::span<const AssetContentVersion> AssetResidencyCoordinator::FindDependents(
		AssetContentVersion dependency) const noexcept
	{
		return m_DependencyGraph.FindDependents(dependency);
	}

	ModelDependencyOutcome AssetResidencyCoordinator::EvaluateModel(
		AssetContentVersion modelVersion) const noexcept
	{
		return m_DependencyGraph.EvaluateModel(modelVersion);
	}

	AssetDependencyGraphStatistics AssetResidencyCoordinator::GetDependencyStatistics() const noexcept
	{
		return m_DependencyGraph.GetStatistics();
	}

	void AssetResidencyCoordinator::RecordDependencyValidation(bool matched) noexcept
	{
		++m_DependencyValidationCount;
		m_DependencyValidationMismatchCount += matched ? 0 : 1;
	}

	uint64_t AssetResidencyCoordinator::GetDependencyValidationCount() const noexcept
	{
		return m_DependencyValidationCount;
	}

	uint64_t AssetResidencyCoordinator::GetDependencyValidationMismatchCount() const noexcept
	{
		return m_DependencyValidationMismatchCount;
	}

	std::vector<AssetResidencyCoordinator::PendingEviction>&
		AssetResidencyCoordinator::PendingEvictions() noexcept
	{
		return m_PendingEvictions;
	}

	const std::vector<AssetResidencyCoordinator::PendingEviction>&
		AssetResidencyCoordinator::PendingEvictions() const noexcept
	{
		return m_PendingEvictions;
	}

	std::vector<AssetResidencyCoordinator::PendingRetirement>&
		AssetResidencyCoordinator::PendingRetirements() noexcept
	{
		return m_PendingRetirements;
	}

	const std::vector<AssetResidencyCoordinator::PendingRetirement>&
		AssetResidencyCoordinator::PendingRetirements() const noexcept
	{
		return m_PendingRetirements;
	}

	void AssetResidencyCoordinator::AddLogicalResidentBytes(uint64_t bytes) noexcept
	{
		m_LogicalResidentBytes += bytes;
	}

	void AssetResidencyCoordinator::SubtractLogicalResidentBytes(uint64_t bytes) noexcept
	{
		m_LogicalResidentBytes = bytes > m_LogicalResidentBytes ? 0 : m_LogicalResidentBytes - bytes;
	}

	uint64_t AssetResidencyCoordinator::GetLogicalResidentBytes() const noexcept
	{
		return m_LogicalResidentBytes;
	}
}
