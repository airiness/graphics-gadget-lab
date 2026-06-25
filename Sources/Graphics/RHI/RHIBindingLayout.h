#pragma once
#include "Graphics/RHI/RHIHandles.h"
#include "Graphics/RHI/RHITypes.h"

#include <array>
#include <cstdint>

namespace gglab
{
	enum class RHIBindingType : uint8_t
	{
		Unknown,
		ConstantBuffer,
		ReadOnlyStorageBuffer,
		ReadWriteStorageBuffer,
		SampledTexture,
		StorageTexture,
		Sampler,
		PushConstants,
		BindlessSampledTextureTable,
		BindlessSamplerTable,
	};

	struct RHIBindingSlotDesc
	{
		RHIBindingType m_Type = RHIBindingType::Unknown;
		RHIShaderStage m_Visibility = RHIShaderStage::None;
		uint32_t m_Binding = 0;
		uint32_t m_Space = 0;
		// A count of 0 means an unbounded/global binding range.
		uint32_t m_Count = 1;
		uint32_t m_SizeInBytes = 0;
		const char* m_DebugName = nullptr;
	};

	struct RHIBindingLayoutDesc
	{
		static constexpr uint32_t MaxSlots = 32;

		std::array<RHIBindingSlotDesc, MaxSlots> m_Slots{};
		uint32_t m_SlotCount = 0;
		const char* m_DebugName = nullptr;
	};
}
