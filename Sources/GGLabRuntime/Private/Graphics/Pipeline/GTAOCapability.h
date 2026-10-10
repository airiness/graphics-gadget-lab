#pragma once

#include "GGLabRuntime/Graphics/Pipeline/GTAO.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

namespace gglab
{
	// Sampled reads and typed UAV stores of one GTAO surface format.
	[[nodiscard]] inline GTAOSurfaceFormatSupport QueryGTAOSurfaceFormatSupport(
		const RHIDevice& device, RHIFormat format) noexcept
	{
		const RHITextureDesc textureDesc{
			.m_Dimension = RHITextureDimension::Texture2D,
			.m_Format = format,
			.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::UnorderedAccess,
			.m_Extent = { 1, 1, 1 },
		};
		auto viewDesc = MakeRHITexture2DViewDesc(format);
		viewDesc.m_Type = RHITextureViewType::ShaderResource;
		const RHITextureSupportResult shaderResource =
			device.QueryTextureViewSupport(textureDesc, viewDesc);
		viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
		return {
			.m_ShaderResource = shaderResource,
			.m_TypedUavStore = device.QueryTextureViewSupport(textureDesc, viewDesc),
		};
	}

	// Device support of every GTAO surface format, independent of program preparation.
	[[nodiscard]] inline GTAOCapabilityStatus QueryGTAOCapabilityStatus(
		const RHIDevice& device) noexcept
	{
		const GTAOSurfaceFormatSupport r16Float =
			QueryGTAOSurfaceFormatSupport(device, RHIFormat::R16Float);
		return {
			.m_R16Float = r16Float,
			.m_R32Float = QueryGTAOSurfaceFormatSupport(device, RHIFormat::R32Float),
			.m_R16G16Float = QueryGTAOSurfaceFormatSupport(device, RHIFormat::R16G16Float),
			.m_R16G16B16A16Float =
				QueryGTAOSurfaceFormatSupport(device, RHIFormat::R16G16B16A16Float),
			.m_FinalAO = ResolveGTAOFinalAOFormat(
				QueryGTAOSurfaceFormatSupport(device, RHIFormat::R8Unorm), r16Float),
		};
	}
}
