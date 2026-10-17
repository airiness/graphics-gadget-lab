#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <array>
#include <cstdint>
#include <cstddef>

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
		BindlessResourceTable,
		BindlessSamplerTable,
	};

	[[nodiscard]] constexpr bool IsBindlessBindingType(RHIBindingType type) noexcept
	{
		return type == RHIBindingType::BindlessResourceTable ||
			type == RHIBindingType::BindlessSamplerTable;
	}

	enum class RHIBufferBindingMode : uint8_t
	{
		Fixed,
		Dynamic,
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
		RHIBufferBindingMode m_BufferBindingMode = RHIBufferBindingMode::Fixed;
	};

	struct RHIBindingLayoutDesc
	{
		static constexpr uint32_t MaxSlots = 32;

		std::array<RHIBindingSlotDesc, MaxSlots> m_Slots{};
		uint32_t m_SlotCount = 0;
		const char* m_DebugName = nullptr;
	};

	[[nodiscard]] constexpr bool IsValidPushConstantWrite(uint32_t sizeInBytes,
		size_t wordCount, uint32_t destOffsetInWords) noexcept
	{
		const uint32_t capacity = sizeInBytes / sizeof(uint32_t);
		return sizeInBytes != 0 && sizeInBytes % sizeof(uint32_t) == 0 &&
			wordCount != 0 && destOffsetInWords <= capacity &&
			wordCount <= capacity - destOffsetInWords;
	}
}
