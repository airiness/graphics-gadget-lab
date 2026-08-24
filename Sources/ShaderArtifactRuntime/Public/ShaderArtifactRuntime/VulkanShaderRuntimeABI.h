#pragma once
#include "ShaderArtifactRuntime/ShaderArtifactTypes.h"

#include <cstdint>

namespace gglab
{
	enum class VulkanShaderRegisterClass : uint8_t
	{
		ConstantBuffer,
		ShaderResource,
		UnorderedAccess,
		Sampler,
	};

	struct VulkanFixedRegisterRange
	{
		uint32_t m_BindingShift = 0;
		uint32_t m_RegisterCount = 0;
	};

	// Shared Vulkan Shader/Runtime compatibility contract. The target environment
	// remains a separate artifact axis; m_Revision versions only the binding ABI.
	struct VulkanShaderRuntimeABI
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

		bool m_InvertVertexProducingStageY = true;
		bool m_UseDxPositionW = true;
	};

	inline constexpr VulkanShaderRuntimeABI GGLabVulkanShaderRuntimeABI{};

	[[nodiscard]] constexpr bool IsVulkanVertexProducingShaderStage(
		ShaderStage stage) noexcept
	{
		return stage == ShaderStage::Vertex || stage == ShaderStage::Domain ||
			stage == ShaderStage::Geometry || stage == ShaderStage::Mesh;
	}

	[[nodiscard]] constexpr ShaderCoordinateOptions GetGGLabVulkanShaderCoordinateOptions(
		ShaderStage stage) noexcept
	{
		ShaderCoordinateOptions options = ShaderCoordinateOptions::None;
		if (IsVulkanVertexProducingShaderStage(stage) &&
			GGLabVulkanShaderRuntimeABI.m_InvertVertexProducingStageY)
		{
			options |= ShaderCoordinateOptions::InvertY;
		}
		if (stage == ShaderStage::Pixel && GGLabVulkanShaderRuntimeABI.m_UseDxPositionW)
		{
			options |= ShaderCoordinateOptions::UseDxPositionW;
		}
		return options;
	}

	[[nodiscard]] constexpr VulkanFixedRegisterRange GetVulkanFixedRegisterRange(
		VulkanShaderRegisterClass registerClass) noexcept
	{
		switch (registerClass)
		{
		case VulkanShaderRegisterClass::ConstantBuffer:
			return GGLabVulkanShaderRuntimeABI.m_ConstantBufferRange;
		case VulkanShaderRegisterClass::ShaderResource:
			return GGLabVulkanShaderRuntimeABI.m_ShaderResourceRange;
		case VulkanShaderRegisterClass::UnorderedAccess:
			return GGLabVulkanShaderRuntimeABI.m_UnorderedAccessRange;
		case VulkanShaderRegisterClass::Sampler:
			return GGLabVulkanShaderRuntimeABI.m_SamplerRange;
		}
		return {};
	}
}
