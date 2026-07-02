#include "Core/Precompiled.h"
#include "Diagnostics/Builders/TransientResourcePoolSnapshotBuilder.h"
#include "Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	namespace
	{
		void AccumulateState(
			TransientPoolStateCounts& counts,
			TransientPoolSlotState state) noexcept
		{
			++counts.m_Total;
			switch (state)
			{
			case TransientPoolSlotState::Leased: ++counts.m_Leased; break;
			case TransientPoolSlotState::PendingRetirement: ++counts.m_PendingRetirement; break;
			case TransientPoolSlotState::Available: ++counts.m_Available; break;
			case TransientPoolSlotState::Destroyed: ++counts.m_Destroyed; break;
			}
		}
	}

	void BuildTransientResourcePoolSnapshot(
		const TransientResourcePool& pool,
		TransientResourcePoolSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		outSnapshot.m_PendingRetirementCount = static_cast<uint32_t>(pool.m_PendingRetirements.size());
		outSnapshot.m_MaxCachedPerKey = pool.m_MaxCachedPerKey;

		std::unordered_set<uint32_t> availableTextures;
		std::unordered_set<uint32_t> availableBuffers;
		for (const auto& [key, slots] : pool.m_FreeTextures)
		{
			GGLAB_UNUSED(key);
			outSnapshot.m_FreeTextureKeyCount += slots.empty() ? 0u : 1u;
			for (const auto slot : slots)
			{
				availableTextures.insert(slot.Value());
			}
		}
		for (const auto& [key, slots] : pool.m_FreeBuffers)
		{
			GGLAB_UNUSED(key);
			outSnapshot.m_FreeBufferKeyCount += slots.empty() ? 0u : 1u;
			for (const auto slot : slots)
			{
				availableBuffers.insert(slot.Value());
			}
		}

		std::unordered_map<uint32_t, RHIFencePoint> pendingTextures;
		std::unordered_map<uint32_t, RHIFencePoint> pendingBuffers;
		for (const auto& pending : pool.m_PendingRetirements)
		{
			if (pending.m_Type == TransientResourcePool::ResourceType::Texture)
			{
				pendingTextures[pending.m_PoolSlot.Value()] = pending.m_FencePoint;
			}
			else
			{
				pendingBuffers[pending.m_PoolSlot.Value()] = pending.m_FencePoint;
			}
		}

		outSnapshot.m_Textures.reserve(pool.m_Textures.size());
		for (uint32_t index = 0; index < pool.m_Textures.size(); ++index)
		{
			const auto& record = pool.m_Textures[index];
			const auto pending = pendingTextures.find(index);
			const bool isPending = pending != pendingTextures.end();
			const auto state = !record.m_Texture.IsValid() ?
				TransientPoolSlotState::Destroyed :
				isPending ? TransientPoolSlotState::PendingRetirement :
				availableTextures.contains(index) ? TransientPoolSlotState::Available :
				TransientPoolSlotState::Leased;

			TransientTextureSlotSnapshot snapshot{};
			snapshot.m_PoolSlot = TransientResourcePoolSlot(index);
			snapshot.m_State = state;
			snapshot.m_Texture = record.m_Texture;
			snapshot.m_Key = record.m_Key;
			if (isPending)
			{
				snapshot.m_RetirementFence = pending->second;
				snapshot.m_RetirementFenceCompleted =
					pool.m_Device && pool.m_Device->IsFencePointCompleted(pending->second);
			}
			outSnapshot.m_Textures.push_back(std::move(snapshot));
			AccumulateState(outSnapshot.m_TextureCounts, state);
		}

		outSnapshot.m_Buffers.reserve(pool.m_Buffers.size());
		for (uint32_t index = 0; index < pool.m_Buffers.size(); ++index)
		{
			const auto& record = pool.m_Buffers[index];
			const auto pending = pendingBuffers.find(index);
			const bool isPending = pending != pendingBuffers.end();
			const auto state = !record.m_Buffer.IsValid() ?
				TransientPoolSlotState::Destroyed :
				isPending ? TransientPoolSlotState::PendingRetirement :
				availableBuffers.contains(index) ? TransientPoolSlotState::Available :
				TransientPoolSlotState::Leased;

			TransientBufferSlotSnapshot snapshot{};
			snapshot.m_PoolSlot = TransientResourcePoolSlot(index);
			snapshot.m_State = state;
			snapshot.m_Buffer = record.m_Buffer;
			snapshot.m_Key = record.m_Key;
			if (isPending)
			{
				snapshot.m_RetirementFence = pending->second;
				snapshot.m_RetirementFenceCompleted =
					pool.m_Device && pool.m_Device->IsFencePointCompleted(pending->second);
			}
			outSnapshot.m_Buffers.push_back(std::move(snapshot));
			AccumulateState(outSnapshot.m_BufferCounts, state);
		}
	}
}
