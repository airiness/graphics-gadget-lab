#pragma once

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

	// gglab Vulkan Shader/Runtime compatibility contract (Shader ABI axis).
	// It does not express the target environment version: m_Revision is the
	// explicit Binding ABI version axis (the only explicit version axis in
	// this contract), while the target environment (Vulkan 1.3, a future 1.4,
	// ...) is selected separately by the target profile through
	// ShaderCompileTarget.m_SpirVTargetEnvironment. A target environment
	// change without an ABI change must not duplicate this contract.
	struct VulkanShaderCompileABI
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

		// Shader compile coordinate policy. These flags share the contract
		// header with the binding numbers but do not share the Binding ABI
		// version axis: coordinate contract changes flow through recipe
		// identity via ShaderCoordinateOptions and do not bump m_Revision by
		// themselves. A future independent Coordinate ABI revision is a
		// decision driven by real compatibility pressure.
		bool m_InvertVertexProducingStageY = true;
		bool m_UseDxPositionW = true;
	};

	inline constexpr VulkanShaderCompileABI GGLabVulkanShaderCompileABI{};

	[[nodiscard]] constexpr VulkanFixedRegisterRange GetVulkanFixedRegisterRange(
		VulkanShaderRegisterClass registerClass) noexcept
	{
		switch (registerClass)
		{
		case VulkanShaderRegisterClass::ConstantBuffer:
			return GGLabVulkanShaderCompileABI.m_ConstantBufferRange;
		case VulkanShaderRegisterClass::ShaderResource:
			return GGLabVulkanShaderCompileABI.m_ShaderResourceRange;
		case VulkanShaderRegisterClass::UnorderedAccess:
			return GGLabVulkanShaderCompileABI.m_UnorderedAccessRange;
		case VulkanShaderRegisterClass::Sampler:
			return GGLabVulkanShaderCompileABI.m_SamplerRange;
		}
		return {};
	}
}
