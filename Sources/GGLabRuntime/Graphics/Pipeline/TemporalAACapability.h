#pragma once

#include "Graphics/Pipeline/TemporalAA.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

namespace gglab
{
	inline constexpr RHIFormat TemporalAAResolvedColorFormat =
		RHIFormat::R16G16B16A16Float;

	struct TemporalAAResolvedColorFormatSupport
	{
		RHITextureSupportResult m_RenderTarget{};
		RHITextureSupportResult m_ShaderResource{};
		RHITextureSupportResult m_TypedUavStore{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_RenderTarget.IsSupported() && m_ShaderResource.IsSupported() &&
				m_TypedUavStore.IsSupported();
		}
	};

	[[nodiscard]] inline TemporalAAResolvedColorFormatSupport
		QueryTemporalAAResolvedColorFormatSupport(const RHIDevice& device) noexcept
	{
		RHITextureDesc textureDesc{};
		textureDesc.m_Dimension = RHITextureDimension::Texture2D;
		textureDesc.m_Format = TemporalAAResolvedColorFormat;
		textureDesc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled |
			RHITextureUsage::UnorderedAccess;
		textureDesc.m_Extent = { 1, 1, 1 };
		RHITextureViewDesc viewDesc =
			MakeRHITexture2DViewDesc(TemporalAAResolvedColorFormat);
		viewDesc.m_Type = RHITextureViewType::RenderTarget;
		const RHITextureSupportResult renderTarget =
			device.QueryTextureViewSupport(textureDesc, viewDesc);
		viewDesc.m_Type = RHITextureViewType::ShaderResource;
		const RHITextureSupportResult shaderResource =
			device.QueryTextureViewSupport(textureDesc, viewDesc);
		viewDesc.m_Type = RHITextureViewType::UnorderedAccess;
		return {
			.m_RenderTarget = renderTarget,
			.m_ShaderResource = shaderResource,
			.m_TypedUavStore = device.QueryTextureViewSupport(textureDesc, viewDesc),
		};
	}
}
