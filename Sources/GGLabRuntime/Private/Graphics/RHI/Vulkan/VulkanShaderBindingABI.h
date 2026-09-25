#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <array>
#include <cstdint>

namespace gglab
{
	enum class VulkanDescriptorType : uint8_t
	{
		Mutable,
		SampledImage,
		StorageImage,
		Sampler,
	};

	enum class VulkanShaderBindingRejectionReason : uint8_t
	{
		None,
		ReservedGlobalHeapRegisterSpace,
		UnsupportedFixedRegisterSpace,
		FixedRegisterIndexOutOfRange,
	};

	struct VulkanShaderBindingLocation
	{
		uint32_t m_DescriptorSet = 0;
		uint32_t m_Binding = 0;
	};

	struct VulkanShaderBindingResult
	{
		VulkanShaderBindingLocation m_Location{};
		VulkanShaderBindingRejectionReason m_RejectionReason =
			VulkanShaderBindingRejectionReason::None;

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_RejectionReason == VulkanShaderBindingRejectionReason::None;
		}
	};

	// Vulkan runtime descriptor policy. Compiler-facing ABI numbers (revision,
	// register spaces, descriptor sets, heap bindings, register ranges, and
	// the compile coordinate flags) live in the shared VulkanShaderRuntimeABI
	// contract; this struct holds only runtime-only descriptor publication
	// policy and consumes the contract through GGLabVulkanShaderRuntimeABI.
	struct VulkanShaderBindingABI
	{
		VulkanDescriptorType m_ResourceHeapDescriptorType = VulkanDescriptorType::Mutable;
		std::array<VulkanDescriptorType, 2> m_ResourceHeapMutableAllowedTypes{
			VulkanDescriptorType::SampledImage,
			VulkanDescriptorType::StorageImage,
		};
		VulkanDescriptorType m_SamplerHeapDescriptorType = VulkanDescriptorType::Sampler;
		RHIDescriptorCapacityContract m_DescriptorCapacity = GGLabDescriptorCapacityContract;
		bool m_PartiallyBound = true;
		bool m_UpdateAfterBind = true;
		bool m_UpdateUnusedWhilePending = true;
	};

	inline constexpr VulkanShaderBindingABI GGLabVulkanShaderBindingABI{};

	[[nodiscard]] constexpr VulkanShaderBindingResult EvaluateVulkanFixedShaderBinding(
		VulkanShaderRegisterClass registerClass, uint32_t registerIndex,
		uint32_t registerSpace) noexcept
	{
		if (registerSpace == GGLabVulkanShaderRuntimeABI.m_GlobalHeapHlslRegisterSpace)
		{
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
			};
		}
		if (registerSpace != GGLabVulkanShaderRuntimeABI.m_FixedHlslRegisterSpace)
		{
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::UnsupportedFixedRegisterSpace,
			};
		}

		const VulkanFixedRegisterRange range = GetVulkanFixedRegisterRange(registerClass);
		if (registerIndex >= range.m_RegisterCount)
		{
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::FixedRegisterIndexOutOfRange,
			};
		}

		return {
			.m_Location = {
				.m_DescriptorSet = GGLabVulkanShaderRuntimeABI.m_FixedDescriptorSet,
				.m_Binding = range.m_BindingShift + registerIndex,
			},
		};
	}
}
