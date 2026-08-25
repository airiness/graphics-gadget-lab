#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "ShaderArtifactRuntime/VulkanShaderRuntimeABI.h"

namespace gglab
{
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
		target.m_BindingABIRevision = GGLabVulkanShaderRuntimeABI.m_Revision;
		target.m_CoordinateOptions = GetGGLabVulkanShaderCoordinateOptions(stage);
		return target;
	}
}
