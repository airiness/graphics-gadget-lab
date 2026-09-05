#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDescriptorCapacityContract.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace gglab
{
	enum class VulkanDescriptorType : uint8_t
	{
		Mutable,
		SampledImage,
		StorageImage,
		Sampler,
	};

	enum class VulkanBindlessResourceClass : uint8_t
	{
		SampledTexture,
		StorageTexture,
		Sampler,
		ConstantBuffer,
		ReadOnlyStorageBuffer,
		ReadWriteStorageBuffer,
		UniformTexelBuffer,
		StorageTexelBuffer,
		CombinedImageSampler,
		AccelerationStructure,
	};

	enum class VulkanShaderBindingRejectionReason : uint8_t
	{
		None,
		ReservedGlobalHeapRegisterSpace,
		UnsupportedFixedRegisterSpace,
		FixedRegisterIndexOutOfRange,
		UnsupportedBindlessResourceClass,
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

	[[nodiscard]] constexpr VulkanShaderBindingResult EvaluateVulkanBindlessShaderBinding(
		VulkanBindlessResourceClass resourceClass) noexcept
	{
		switch (resourceClass)
		{
		case VulkanBindlessResourceClass::SampledTexture:
		case VulkanBindlessResourceClass::StorageTexture:
			return {
				.m_Location = {
					.m_DescriptorSet = GGLabVulkanShaderRuntimeABI.m_GlobalDescriptorSet,
					.m_Binding = GGLabVulkanShaderRuntimeABI.m_ResourceHeapBinding,
				},
			};
		case VulkanBindlessResourceClass::Sampler:
			return {
				.m_Location = {
					.m_DescriptorSet = GGLabVulkanShaderRuntimeABI.m_GlobalDescriptorSet,
					.m_Binding = GGLabVulkanShaderRuntimeABI.m_SamplerHeapBinding,
				},
			};
		default:
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::UnsupportedBindlessResourceClass,
			};
		}
	}

	[[nodiscard]] constexpr bool IsVulkanBindlessResourceClassSupported(
		VulkanBindlessResourceClass resourceClass) noexcept
	{
		return EvaluateVulkanBindlessShaderBinding(resourceClass).IsSupported();
	}

	[[nodiscard]] constexpr std::string_view VulkanDescriptorTypeName(
		VulkanDescriptorType type) noexcept
	{
		switch (type)
		{
		case VulkanDescriptorType::Mutable:
			return "VK_DESCRIPTOR_TYPE_MUTABLE_EXT";
		case VulkanDescriptorType::SampledImage:
			return "VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE";
		case VulkanDescriptorType::StorageImage:
			return "VK_DESCRIPTOR_TYPE_STORAGE_IMAGE";
		case VulkanDescriptorType::Sampler:
			return "VK_DESCRIPTOR_TYPE_SAMPLER";
		}
		return "VK_DESCRIPTOR_TYPE_UNKNOWN";
	}

	[[nodiscard]] constexpr std::string_view VulkanShaderBindingRejectionReasonText(
		VulkanShaderBindingRejectionReason reason) noexcept
	{
		switch (reason)
		{
		case VulkanShaderBindingRejectionReason::None:
			return "none";
		case VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace:
			return "HLSL space1 is reserved for the global descriptor heaps";
		case VulkanShaderBindingRejectionReason::UnsupportedFixedRegisterSpace:
			return "fixed shader bindings only support HLSL space0";
		case VulkanShaderBindingRejectionReason::FixedRegisterIndexOutOfRange:
			return "shader register index exceeds its fixed Vulkan binding range";
		case VulkanShaderBindingRejectionReason::UnsupportedBindlessResourceClass:
			return "bindless resource class is not supported by Vulkan binding ABI revision 1";
		}
		return "unknown shader binding rejection reason";
	}
}
