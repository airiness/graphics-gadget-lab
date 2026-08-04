#pragma once
#include "Graphics/RHI/RHIDescriptorCapacityContract.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace gglab
{
	enum class VulkanShaderRegisterClass : uint8_t
	{
		ConstantBuffer,
		ShaderResource,
		UnorderedAccess,
		Sampler,
	};

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

	struct VulkanFixedRegisterRange
	{
		uint32_t m_BindingShift = 0;
		uint32_t m_RegisterCount = 0;
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

	struct VulkanShaderBindingABI
	{
		uint32_t m_Revision = 1;

		uint32_t m_FixedHlslRegisterSpace = 0;
		uint32_t m_GlobalHeapHlslRegisterSpace = 1;

		uint32_t m_FixedDescriptorSet = 0;
		uint32_t m_GlobalDescriptorSet = 1;
		uint32_t m_ResourceHeapBinding = 0;
		uint32_t m_SamplerHeapBinding = 1;

		VulkanFixedRegisterRange m_ConstantBufferRange{ 0, 32 };
		VulkanFixedRegisterRange m_ShaderResourceRange{ 32, 32 };
		VulkanFixedRegisterRange m_UnorderedAccessRange{ 64, 32 };
		VulkanFixedRegisterRange m_SamplerRange{ 96, 32 };

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

	[[nodiscard]] constexpr VulkanFixedRegisterRange GetVulkanFixedRegisterRange(
		VulkanShaderRegisterClass registerClass) noexcept
	{
		switch (registerClass)
		{
		case VulkanShaderRegisterClass::ConstantBuffer:
			return GGLabVulkanShaderBindingABI.m_ConstantBufferRange;
		case VulkanShaderRegisterClass::ShaderResource:
			return GGLabVulkanShaderBindingABI.m_ShaderResourceRange;
		case VulkanShaderRegisterClass::UnorderedAccess:
			return GGLabVulkanShaderBindingABI.m_UnorderedAccessRange;
		case VulkanShaderRegisterClass::Sampler:
			return GGLabVulkanShaderBindingABI.m_SamplerRange;
		}
		return {};
	}

	[[nodiscard]] constexpr VulkanShaderBindingResult EvaluateVulkanFixedShaderBinding(
		VulkanShaderRegisterClass registerClass, uint32_t registerIndex,
		uint32_t registerSpace) noexcept
	{
		if (registerSpace == GGLabVulkanShaderBindingABI.m_GlobalHeapHlslRegisterSpace)
		{
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::ReservedGlobalHeapRegisterSpace,
			};
		}
		if (registerSpace != GGLabVulkanShaderBindingABI.m_FixedHlslRegisterSpace)
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
				.m_DescriptorSet = GGLabVulkanShaderBindingABI.m_FixedDescriptorSet,
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
					.m_DescriptorSet = GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet,
					.m_Binding = GGLabVulkanShaderBindingABI.m_ResourceHeapBinding,
				},
			};
		case VulkanBindlessResourceClass::Sampler:
			return {
				.m_Location = {
					.m_DescriptorSet = GGLabVulkanShaderBindingABI.m_GlobalDescriptorSet,
					.m_Binding = GGLabVulkanShaderBindingABI.m_SamplerHeapBinding,
				},
			};
		default:
			return {
				.m_RejectionReason =
					VulkanShaderBindingRejectionReason::UnsupportedBindlessResourceClass,
			};
		}
	}

	[[nodiscard]] constexpr bool IsVulkanV1BindlessResourceClassSupported(
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
			return "shader register index exceeds its Vulkan v1 fixed-binding range";
		case VulkanShaderBindingRejectionReason::UnsupportedBindlessResourceClass:
			return "bindless resource class is not supported by Vulkan binding ABI revision 1";
		}
		return "unknown shader binding rejection reason";
	}
}
