#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <array>
#include <cstdint>

namespace gglab
{
	inline constexpr RHIFormat TemporalMotionFormat = RHIFormat::R16G16Float;
	inline constexpr std::array<float, 4> TemporalMotionClearColor{ 0.0f, 0.0f, 0.0f, 0.0f };

	struct TemporalMotionFormatSupport
	{
		RHITextureSupportResult m_RenderTarget{};
		RHITextureSupportResult m_ShaderResource{};

		[[nodiscard]] constexpr bool IsSupported() const noexcept
		{
			return m_RenderTarget.IsSupported() && m_ShaderResource.IsSupported();
		}
	};

	[[nodiscard]] inline RHITextureDesc MakeTemporalMotionTextureDesc(
		uint32_t width, uint32_t height) noexcept
	{
		RHITextureDesc desc{};
		desc.m_Dimension = RHITextureDimension::Texture2D;
		desc.m_Format = TemporalMotionFormat;
		desc.m_Extent = { width, height, 1 };
		desc.m_ClearValue = RHIClearValue{
			.m_Format = TemporalMotionFormat,
			.m_Color = { TemporalMotionClearColor[0], TemporalMotionClearColor[1],
				TemporalMotionClearColor[2], TemporalMotionClearColor[3] },
		};
		return desc;
	}

	[[nodiscard]] inline TemporalMotionFormatSupport QueryTemporalMotionFormatSupport(
		const RHIDevice& device) noexcept
	{
		RHITextureDesc textureDesc = MakeTemporalMotionTextureDesc(1, 1);
		textureDesc.m_Usage = RHITextureUsage::RenderTarget | RHITextureUsage::Sampled;
		RHITextureViewDesc viewDesc = MakeRHITexture2DViewDesc(TemporalMotionFormat);
		viewDesc.m_Type = RHITextureViewType::RenderTarget;
		const RHITextureSupportResult renderTarget =
			device.QueryTextureViewSupport(textureDesc, viewDesc);
		viewDesc.m_Type = RHITextureViewType::ShaderResource;
		return {
			.m_RenderTarget = renderTarget,
			.m_ShaderResource = device.QueryTextureViewSupport(textureDesc, viewDesc),
		};
	}
}
