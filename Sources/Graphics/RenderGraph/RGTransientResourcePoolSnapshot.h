#pragma once
#include "Graphics/RenderGraph/RGTransientResourcePool.h"

namespace gglab
{
	enum class RGTransientPoolSlotState : uint8_t
	{
		Leased,
		PendingRetirement,
		Available,
		Destroyed,
	};

	struct RGTransientPoolStateCounts
	{
		uint32_t m_Total = 0;
		uint32_t m_Leased = 0;
		uint32_t m_PendingRetirement = 0;
		uint32_t m_Available = 0;
		uint32_t m_Destroyed = 0;
	};

	struct RGTransientTextureSlotSnapshot
	{
		RGTransientResourcePoolSlot m_PoolSlot{};
		RGTransientPoolSlotState m_State = RGTransientPoolSlotState::Destroyed;
		RHITextureHandle m_Texture{};
		RGTransientTextureKey m_Key{};
		RHIFencePoint m_RetirementFence{};
		bool m_RetirementFenceCompleted = false;
	};

	struct RGTransientBufferSlotSnapshot
	{
		RGTransientResourcePoolSlot m_PoolSlot{};
		RGTransientPoolSlotState m_State = RGTransientPoolSlotState::Destroyed;
		RHIBufferHandle m_Buffer{};
		RGTransientBufferKey m_Key{};
		RHIFencePoint m_RetirementFence{};
		bool m_RetirementFenceCompleted = false;
	};

	struct RGTransientResourcePoolSnapshot
	{
		RGTransientPoolStateCounts m_TextureCounts{};
		RGTransientPoolStateCounts m_BufferCounts{};
		uint32_t m_PendingRetirementCount = 0;
		uint32_t m_FreeTextureKeyCount = 0;
		uint32_t m_FreeBufferKeyCount = 0;
		uint32_t m_MaxCachedPerKey = 0;
		std::vector<RGTransientTextureSlotSnapshot> m_Textures;
		std::vector<RGTransientBufferSlotSnapshot> m_Buffers;
	};

	void BuildRGTransientResourcePoolSnapshot(
		const RGTransientResourcePool& pool,
		RGTransientResourcePoolSnapshot& outSnapshot) noexcept;
}
