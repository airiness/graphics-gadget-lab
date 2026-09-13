#pragma once
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RHI/RHITextureViewDescUtils.h"

#include <array>
#include <cmath>
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

	struct TemporalMotionReadbackSample
	{
		Vector2 m_MotionUV = Vector2::Zero;
		Vector2 m_MotionPixels = Vector2::Zero;
		float m_MagnitudePixels = 0.0f;
		bool m_Valid = false;
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

	[[nodiscard]] inline Vector2 TemporalClipPositionToUV(const Vector4& clipPosition) noexcept
	{
		if (!std::isfinite(clipPosition.m_X) || !std::isfinite(clipPosition.m_Y) ||
			!std::isfinite(clipPosition.m_W) || std::abs(clipPosition.m_W) <= 1.0e-6f)
		{
			return Vector2::Zero;
		}
		const float inverseW = 1.0f / clipPosition.m_W;
		return Vector2(clipPosition.m_X * inverseW * 0.5f + 0.5f,
			-clipPosition.m_Y * inverseW * 0.5f + 0.5f);
	}

	[[nodiscard]] inline Vector2 ComputeTemporalMotionUV(
		const Vector4& currentClip, const Vector4& previousClip) noexcept
	{
		return TemporalClipPositionToUV(currentClip) - TemporalClipPositionToUV(previousClip);
	}

	[[nodiscard]] inline TemporalMotionReadbackSample ResolveTemporalMotionReadbackSample(
		const Vector2& motionUV, uint32_t width, uint32_t height) noexcept
	{
		if (!std::isfinite(motionUV.m_X) || !std::isfinite(motionUV.m_Y) || width == 0 || height == 0)
		{
			return {};
		}
		const Vector2 motionPixels(
			motionUV.m_X * static_cast<float>(width), motionUV.m_Y * static_cast<float>(height));
		return {
			.m_MotionUV = motionUV,
			.m_MotionPixels = motionPixels,
			.m_MagnitudePixels = std::sqrt(
				motionPixels.m_X * motionPixels.m_X + motionPixels.m_Y * motionPixels.m_Y),
			.m_Valid = true,
		};
	}
}
