#include "Core/Precompiled.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"

#include <algorithm>

namespace gglab
{
	namespace
	{
		[[nodiscard]] TaskPriority HigherPriority(
			TaskPriority lhs,
			TaskPriority rhs) noexcept
		{
			return static_cast<uint8_t>(lhs) < static_cast<uint8_t>(rhs) ? lhs : rhs;
		}
	}

	AssetOwnerId AssetInterestTracker::RegisterOwner(std::string label) noexcept
	{
		const AssetOwnerId owner{ m_NextOwnerId++ };
		m_Owners.emplace(owner, std::move(label));
		return owner;
	}

	void AssetInterestTracker::UnregisterOwner(AssetOwnerId owner) noexcept
	{
		GGLAB_ASSERT_MSG(
			std::ranges::none_of(m_Leases,
				[owner](const auto& entry) noexcept
				{
					return entry.second.m_Owner == owner;
				}),
			"Asset owner unregistered while leases are still active.");
		m_Owners.erase(owner);
	}

	AssetLeaseAcquireResult AssetInterestTracker::AcquireLease(
		AssetOwnerId owner,
		AssetContentVersion contentVersion,
		TaskPriority priority,
		bool internal) noexcept
	{
		if (!owner.IsValid() || !contentVersion.IsValid() ||
			priority == TaskPriority::Count || !m_Owners.contains(owner))
		{
			GGLAB_ASSERT_MSG(
				!owner.IsValid() || m_Owners.contains(owner),
				"Asset lease acquired for an unknown owner.");
			return {};
		}

		auto [interest, inserted] = m_Interests.try_emplace(contentVersion.m_Key);
		if (inserted)
		{
			interest->second.m_ContentGeneration = contentVersion.m_ContentGeneration;
			interest->second.m_EffectivePriority = priority;
		}
		else if (interest->second.m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			GGLAB_ASSERT_MSG(false, "Asset interest generation mismatch.");
			return {};
		}

		const TaskPriority previousPriority = interest->second.m_EffectivePriority;
		const bool wasActive = !interest->second.m_LeaseTokens.empty();
		const uint64_t token = m_NextLeaseToken++;
		m_Leases.emplace(token, LeaseRecord{
			.m_ContentVersion = contentVersion,
			.m_Owner = owner,
			.m_Priority = priority,
			.m_IsInternal = internal,
		});
		interest->second.m_LeaseTokens.insert(token);
		return {
			.m_LeaseToken = token,
			.m_Change = RecomputeInterest(
				contentVersion.m_Key,
				previousPriority,
				wasActive),
		};
	}

	std::optional<AssetInterestChange> AssetInterestTracker::ReleaseLease(
		uint64_t leaseToken) noexcept
	{
		const auto lease = m_Leases.find(leaseToken);
		if (lease == m_Leases.end())
		{
			return std::nullopt;
		}
		const LeaseRecord record = lease->second;
		m_Leases.erase(lease);

		const auto interest = m_Interests.find(record.m_ContentVersion.m_Key);
		if (interest == m_Interests.end())
		{
			GGLAB_ASSERT_MSG(false, "Asset lease has no matching interest record.");
			return std::nullopt;
		}
		const TaskPriority previousPriority = interest->second.m_EffectivePriority;
		interest->second.m_LeaseTokens.erase(leaseToken);
		if (interest->second.m_LeaseTokens.empty())
		{
			m_Interests.erase(interest);
			return AssetInterestChange{
				.m_ContentVersion = record.m_ContentVersion,
				.m_PreviousPriority = previousPriority,
				.m_EffectivePriority = previousPriority,
				.m_WasActive = true,
				.m_IsActive = false,
			};
		}
		return RecomputeInterest(
			record.m_ContentVersion.m_Key,
			previousPriority,
			true);
	}

	std::optional<AssetInterestChange> AssetInterestTracker::UpdateLeasePriority(
		uint64_t leaseToken,
		TaskPriority priority) noexcept
	{
		const auto lease = m_Leases.find(leaseToken);
		if (lease == m_Leases.end() || priority == TaskPriority::Count)
		{
			return std::nullopt;
		}
		const AssetKey key = lease->second.m_ContentVersion.m_Key;
		const TaskPriority previousPriority = GetEffectivePriority(key);
		if (lease->second.m_Priority != priority)
		{
			lease->second.m_Priority = priority;
		}
		return RecomputeInterest(key, previousPriority, true);
	}

	bool AssetInterestTracker::AcquirePublicationRetain(
		AssetContentVersion contentVersion) noexcept
	{
		if (!contentVersion.IsValid())
		{
			return false;
		}
		auto [retain, inserted] = m_PublicationRetains.try_emplace(contentVersion.m_Key);
		if (inserted)
		{
			retain->second.m_ContentGeneration = contentVersion.m_ContentGeneration;
		}
		else if (retain->second.m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			GGLAB_ASSERT_MSG(false, "Publication retain generation mismatch.");
			return false;
		}
		++retain->second.m_Count;
		++m_PublicationRetainCount;
		return true;
	}

	void AssetInterestTracker::ReleasePublicationRetain(
		AssetContentVersion contentVersion) noexcept
	{
		const auto retain = m_PublicationRetains.find(contentVersion.m_Key);
		if (retain == m_PublicationRetains.end() ||
			retain->second.m_ContentGeneration != contentVersion.m_ContentGeneration ||
			retain->second.m_Count == 0)
		{
			GGLAB_ASSERT_MSG(false, "Released an unknown publication retain.");
			return;
		}
		--retain->second.m_Count;
		--m_PublicationRetainCount;
		if (retain->second.m_Count == 0)
		{
			m_PublicationRetains.erase(retain);
		}
	}

	bool AssetInterestTracker::HasPublicationRetain(
		AssetContentVersion contentVersion) const noexcept
	{
		const auto retain = m_PublicationRetains.find(contentVersion.m_Key);
		return retain != m_PublicationRetains.end() &&
			retain->second.m_ContentGeneration == contentVersion.m_ContentGeneration &&
			retain->second.m_Count > 0;
	}

	bool AssetInterestTracker::HasActiveInterest(AssetKey key) const noexcept
	{
		const auto interest = m_Interests.find(key);
		return interest != m_Interests.end() && !interest->second.m_LeaseTokens.empty();
	}

	TaskPriority AssetInterestTracker::GetEffectivePriority(
		AssetKey key,
		TaskPriority fallback) const noexcept
	{
		const auto interest = m_Interests.find(key);
		return interest != m_Interests.end() ?
			interest->second.m_EffectivePriority : fallback;
	}

	AssetInterestTrackerStatistics AssetInterestTracker::GetStatistics() const
	{
		AssetInterestTrackerStatistics statistics{
			.m_OwnerCount = static_cast<uint32_t>(m_Owners.size()),
			.m_LeaseCount = static_cast<uint32_t>(m_Leases.size()),
			.m_ManagedAssetCount = static_cast<uint32_t>(m_Interests.size()),
			.m_PriorityUpdateCount = m_PriorityUpdateCount,
			.m_PublicationRetainCount = m_PublicationRetainCount,
		};
		statistics.m_ActiveInterests.reserve(m_Interests.size());
		for (const auto& [key, interest] : m_Interests)
		{
			std::unordered_set<AssetOwnerId, AssetOwnerIdHash> owners;
			for (uint64_t token : interest.m_LeaseTokens)
			{
				if (const auto lease = m_Leases.find(token); lease != m_Leases.end())
				{
					owners.insert(lease->second.m_Owner);
				}
			}
			statistics.m_ActiveInterests.push_back({
				.m_ContentVersion = MakeAssetContentVersion(
					key,
					interest.m_ContentGeneration),
				.m_LeaseCount = static_cast<uint32_t>(interest.m_LeaseTokens.size()),
				.m_OwnerCount = static_cast<uint32_t>(owners.size()),
				.m_EffectivePriority = interest.m_EffectivePriority,
			});
		}
		return statistics;
	}

	AssetInterestChange AssetInterestTracker::RecomputeInterest(
		AssetKey key,
		TaskPriority previousPriority,
		bool wasActive) noexcept
	{
		const auto interest = m_Interests.find(key);
		if (interest == m_Interests.end() || interest->second.m_LeaseTokens.empty())
		{
			return {};
		}
		TaskPriority effective = TaskPriority::Background;
		for (uint64_t token : interest->second.m_LeaseTokens)
		{
			if (const auto lease = m_Leases.find(token); lease != m_Leases.end())
			{
				effective = HigherPriority(effective, lease->second.m_Priority);
			}
		}
		interest->second.m_EffectivePriority = effective;
		if (wasActive && previousPriority != effective)
		{
			++m_PriorityUpdateCount;
		}
		return {
			.m_ContentVersion = MakeAssetContentVersion(
				key,
				interest->second.m_ContentGeneration),
			.m_PreviousPriority = previousPriority,
			.m_EffectivePriority = effective,
			.m_WasActive = wasActive,
			.m_IsActive = true,
		};
	}
}
