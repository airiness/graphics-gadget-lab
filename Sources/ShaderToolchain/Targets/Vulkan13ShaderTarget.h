#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "Targets/VulkanShaderCompileABI.h"

namespace gglab
{
	[[nodiscard]] constexpr bool IsVertexProducingStage(ShaderStage stage) noexcept
	{
		return stage == ShaderStage::Vertex || stage == ShaderStage::Domain ||
			stage == ShaderStage::Geometry || stage == ShaderStage::Mesh;
	}

	// Vulkan 1.3 target policy. Returns the backend-owned target fields only
	// (binaryFormat / spirvEnvironment / bindingAbiRevision /
	// coordinateOptions). The profile selects the Vulkan 1.3 environment while
	// the binding and coordinate contract values come from the shared
	// VulkanShaderCompileABI single source of truth.
	[[nodiscard]] inline ShaderCompileTarget MakeVulkan13CompileTarget(
		ShaderStage stage) noexcept
	{
		ShaderCompileTarget target{};
		target.m_BinaryFormat = ShaderBinaryFormat::SpirV;
		target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::Vulkan1_3;
		target.m_BindingABIRevision = GGLabVulkanShaderCompileABI.m_Revision;
		if (IsVertexProducingStage(stage) &&
			GGLabVulkanShaderCompileABI.m_InvertVertexProducingStageY)
		{
			target.m_CoordinateOptions |= ShaderCoordinateOptions::InvertY;
		}
		if (stage == ShaderStage::Pixel && GGLabVulkanShaderCompileABI.m_UseDxPositionW)
		{
			target.m_CoordinateOptions |= ShaderCoordinateOptions::UseDxPositionW;
		}
		return target;
	}
}
