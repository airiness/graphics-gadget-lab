#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/AssetIdentity.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gglab
{
	struct AssetOwnerId
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		friend constexpr auto operator<=>(const AssetOwnerId&, const AssetOwnerId&) = default;
	};

	struct AssetOwnerIdHash
	{
		size_t operator()(AssetOwnerId owner) const noexcept
		{
			return std::hash<uint64_t>{}(owner.m_Value);
		}
	};

	struct AssetInterestChange
	{
		AssetContentVersion m_ContentVersion{};
		TaskPriority m_PreviousPriority = TaskPriority::Normal;
		TaskPriority m_EffectivePriority = TaskPriority::Normal;
		bool m_WasActive = false;
		bool m_IsActive = false;

		[[nodiscard]] bool IsValid() const noexcept { return m_ContentVersion.IsValid(); }

		[[nodiscard]] bool EffectivePriorityChanged() const noexcept
		{
			return m_WasActive && m_IsActive && m_PreviousPriority != m_EffectivePriority;
		}
	};

	struct AssetLeaseAcquireResult
	{
		uint64_t m_LeaseToken = 0;
		AssetInterestChange m_Change{};

		[[nodiscard]] bool IsValid() const noexcept { return m_LeaseToken != 0; }
	};

	struct TrackedAssetInterestActivity
	{
		AssetContentVersion m_ContentVersion{};
		uint32_t m_LeaseCount = 0;
		uint32_t m_OwnerCount = 0;
		TaskPriority m_EffectivePriority = TaskPriority::Normal;
	};

	struct AssetInterestTrackerStatistics
	{
		uint32_t m_OwnerCount = 0;
		uint32_t m_LeaseCount = 0;
		uint32_t m_ManagedAssetCount = 0;
		uint64_t m_PriorityUpdateCount = 0;
		uint64_t m_PublicationRetainCount = 0;
		std::vector<TrackedAssetInterestActivity> m_ActiveInterests;
	};

	class AssetInterestTracker final
	{
	public:
		[[nodiscard]] AssetOwnerId RegisterOwner() noexcept;
		void UnregisterOwner(AssetOwnerId owner) noexcept;

		[[nodiscard]] AssetLeaseAcquireResult AcquireLease(
			AssetOwnerId owner, AssetContentVersion contentVersion, TaskPriority priority) noexcept;
		[[nodiscard]] std::optional<AssetInterestChange> ReleaseLease(uint64_t leaseToken) noexcept;
		[[nodiscard]] std::optional<AssetInterestChange> UpdateLeasePriority(
			uint64_t leaseToken, TaskPriority priority) noexcept;

		[[nodiscard]] bool AcquirePublicationRetain(AssetContentVersion contentVersion) noexcept;
		void ReleasePublicationRetain(AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] bool HasPublicationRetain(AssetContentVersion contentVersion) const noexcept;

		[[nodiscard]] bool HasActiveInterest(AssetKey key) const noexcept;
		[[nodiscard]] TaskPriority GetEffectivePriority(
			AssetKey key, TaskPriority fallback = TaskPriority::Normal) const noexcept;
		[[nodiscard]] AssetInterestTrackerStatistics GetStatistics() const;

		[[nodiscard]] bool HasOwners() const noexcept { return !m_Owners.empty(); }
		[[nodiscard]] bool HasLeases() const noexcept { return !m_Leases.empty(); }
		[[nodiscard]] bool HasInterests() const noexcept { return !m_Interests.empty(); }
		[[nodiscard]] bool HasPublicationRetains() const noexcept
		{
			return !m_PublicationRetains.empty();
		}

	private:
		struct LeaseRecord
		{
			AssetContentVersion m_ContentVersion{};
			AssetOwnerId m_Owner{};
			TaskPriority m_Priority = TaskPriority::Normal;
		};

		struct InterestRecord
		{
			uint64_t m_ContentGeneration = 0;
			TaskPriority m_EffectivePriority = TaskPriority::Normal;
			std::unordered_set<uint64_t> m_LeaseTokens;
		};

		struct PublicationRetainRecord
		{
			uint64_t m_ContentGeneration = 0;
			uint32_t m_Count = 0;
		};

		[[nodiscard]] AssetInterestChange RecomputeInterest(
			AssetKey key, TaskPriority previousPriority, bool wasActive) noexcept;

		uint64_t m_NextOwnerId = 1;
		uint64_t m_NextLeaseToken = 1;
		std::unordered_set<AssetOwnerId, AssetOwnerIdHash> m_Owners;
		std::unordered_map<uint64_t, LeaseRecord> m_Leases;
		std::unordered_map<AssetKey, InterestRecord, AssetKeyHash> m_Interests;
		std::unordered_map<AssetKey, PublicationRetainRecord, AssetKeyHash> m_PublicationRetains;
		uint64_t m_PriorityUpdateCount = 0;
		uint64_t m_PublicationRetainCount = 0;
	};
}
