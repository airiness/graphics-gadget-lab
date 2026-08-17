#pragma once
#include "Contracts/ShaderCompileTarget.h"
#include "Contracts/ShaderCompileTypes.h"
#include "GGLabFoundation/Base/CoreMacros.h"

namespace gglab
{
	// DX12 target policy. Returns the backend-owned target fields only
	// (binaryFormat / spirvEnvironment / bindingAbiRevision /
	// coordinateOptions); authoring fields belong to the request and the
	// default configuration and are never touched here.
	[[nodiscard]] inline ShaderCompileTarget MakeDX12CompileTarget(ShaderStage stage) noexcept
	{
		GGLAB_UNUSED(stage);
		ShaderCompileTarget target{};
		target.m_BinaryFormat = ShaderBinaryFormat::Dxil;
		target.m_SpirVTargetEnvironment = ShaderSpirVTargetEnvironment::None;
		target.m_BindingABIRevision = 0;
		target.m_CoordinateOptions = ShaderCoordinateOptions::None;
		return target;
	}
}
