#pragma once
#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/Resource/TransientResourcePool.h"

namespace gglab
{
	enum class TransientPoolSlotState : uint8_t
	{
		Leased,
		PendingRetirement,
		Available,
		Destroyed,
	};

	struct TransientPoolStateCounts
	{
		uint32_t m_Total = 0;
		uint32_t m_Leased = 0;
		uint32_t m_PendingRetirement = 0;
		uint32_t m_Available = 0;
		uint32_t m_Destroyed = 0;
	};

	struct TransientTextureSlotSnapshot
	{
		TransientResourcePoolSlot m_PoolSlot{};
		TransientPoolSlotState m_State = TransientPoolSlotState::Destroyed;
		RHITextureHandle m_Texture{};
		TransientTextureKey m_Key{};
		RHIFencePoint m_RetirementFence{};
		bool m_RetirementFenceCompleted = false;
	};

	struct TransientBufferSlotSnapshot
	{
		TransientResourcePoolSlot m_PoolSlot{};
		TransientPoolSlotState m_State = TransientPoolSlotState::Destroyed;
		RHIBufferHandle m_Buffer{};
		TransientBufferKey m_Key{};
		RHIFencePoint m_RetirementFence{};
		bool m_RetirementFenceCompleted = false;
	};

	struct TransientResourcePoolSnapshot
	{
		TransientPoolStateCounts m_TextureCounts{};
		TransientPoolStateCounts m_BufferCounts{};
		uint32_t m_PendingRetirementCount = 0;
		uint32_t m_FreeTextureKeyCount = 0;
		uint32_t m_FreeBufferKeyCount = 0;
		uint32_t m_MaxCachedPerKey = 0;
		std::vector<TransientTextureSlotSnapshot> m_Textures;
		std::vector<TransientBufferSlotSnapshot> m_Buffers;
	};

	template<>
	struct SnapshotTraits<TransientResourcePoolSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.TransientResourcePoolSnapshot");
	};
}
