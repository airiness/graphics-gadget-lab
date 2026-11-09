#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"

#include <bit>
#include <cstdint>

namespace gglab
{
	// Optional capability set, independent of the mandatory raster device profile.
	struct VulkanRayQueryAvailability
	{
		bool m_AccelerationStructureExtension = false;
		bool m_RayQueryExtension = false;
		bool m_DeferredHostOperationsExtension = false;
		bool m_AccelerationStructure = false;
		bool m_RayQuery = false;
		bool m_BufferDeviceAddress = false;
		RHIASBuildLimits m_BuildLimits{};
		uint64_t m_MaxGeometryCount = 0;
		uint32_t m_MaxDescriptorSetAccelerationStructures = 0;
		uint32_t m_MaxPerStageDescriptorAccelerationStructures = 0;
	};

	[[nodiscard]] constexpr RHIRayQueryCapabilities EvaluateVulkanRayQuerySupport(
		const VulkanRayQueryAvailability& available) noexcept
	{
		RHIRayQueryCapabilities result{};
		if (!available.m_AccelerationStructureExtension || !available.m_RayQueryExtension ||
			!available.m_DeferredHostOperationsExtension)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::RequiredExtensionsUnavailable;
		}
		else if (!available.m_AccelerationStructure)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::AccelerationStructureUnavailable;
		}
		else if (!available.m_RayQuery)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::InlineQueryUnavailable;
		}
		else if (!available.m_BufferDeviceAddress)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::DeviceAddressUnavailable;
		}
		else if (available.m_BuildLimits.m_MaxPrimitiveCount == 0 ||
			available.m_BuildLimits.m_MaxInstanceCount == 0 ||
			!std::has_single_bit(available.m_BuildLimits.m_ScratchAlignmentInBytes) ||
			available.m_MaxGeometryCount == 0 ||
			available.m_MaxDescriptorSetAccelerationStructures == 0 ||
			available.m_MaxPerStageDescriptorAccelerationStructures == 0)
		{
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::BuildLimitsUnavailable;
		}
		else
		{
			result.m_Supported = true;
			result.m_UnavailableReason = RHIRayQueryUnavailableReason::None;
			result.m_BuildLimits = available.m_BuildLimits;
		}
		return result;
	}
}
