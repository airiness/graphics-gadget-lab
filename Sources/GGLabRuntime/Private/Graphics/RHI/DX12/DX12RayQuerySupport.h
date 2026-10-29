#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"

#include <d3d12.h>
#include <limits>

namespace gglab
{
	[[nodiscard]] constexpr uint64_t AlignDX12ASStorageSize(uint64_t minimumSize) noexcept
	{
		constexpr uint64_t mask = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT - 1;
		return minimumSize > std::numeric_limits<uint64_t>::max() - mask ?
			0 : (minimumSize + mask) & ~mask;
	}

	[[nodiscard]] constexpr RHIRayQueryCapabilities EvaluateDX12RayQuerySupport(
		D3D12_RAYTRACING_TIER tier, D3D_SHADER_MODEL shaderModel) noexcept
	{
		RHIRayQueryCapabilities result{};
		if (tier < D3D12_RAYTRACING_TIER_1_1)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::InlineQueryUnavailable;
			return result;
		}
		if (shaderModel < D3D_SHADER_MODEL_6_5)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::ShaderModelUnavailable;
			return result;
		}
		result.m_Supported = true;
		result.m_UnavailableReason = RHIRayQueryUnavailableReason::None;
		result.m_BuildLimits = {
			.m_MaxPrimitiveCount = D3D12_RAYTRACING_MAX_PRIMITIVES_PER_BOTTOM_LEVEL_ACCELERATION_STRUCTURE,
			.m_MaxInstanceCount = D3D12_RAYTRACING_MAX_INSTANCES_PER_TOP_LEVEL_ACCELERATION_STRUCTURE,
			.m_ScratchAlignmentInBytes = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT,
		};
		return result;
	}
}
