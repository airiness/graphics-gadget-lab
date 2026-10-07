#pragma once

#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <cstdint>

namespace gglab
{
	// Evaluation-only supersampled reference. Each sample renders the display view with
	// one reference jitter phase while Temporal AA is inactive, and adds the frame's HDR
	// scene color (after transparent and depth-tested debug geometry, before
	// post-processing) to an RGBA32F sum. Post-processing receives the running mean, so
	// the last sample of a reference presents the mean of every sample.
	inline constexpr uint32_t MaxTemporalReferenceSamples = 4096;
	inline constexpr RHIFormat TemporalReferenceSumFormat = RHIFormat::R32G32B32A32Float;

	struct TemporalReferenceSample
	{
		// Zero starts a new sum; each later sample must follow the previous submitted one.
		uint32_t m_Index = 0;
		uint32_t m_Count = 0;
		// Material texture LOD bias of every sample. Zero filters textures for the whole
		// pixel before the samples are averaged; -0.5 * log2(m_Count) matches each
		// sample's sub-pixel footprint instead.
		float m_TextureLodBias = 0.0f;

		[[nodiscard]] constexpr bool IsValid() const noexcept
		{
			return m_Count > 0 && m_Count <= MaxTemporalReferenceSamples && m_Index < m_Count;
		}

		bool operator==(const TemporalReferenceSample&) const noexcept = default;
	};

	// Persistent sum pair imported by the accumulation pass of one reference sample.
	struct TemporalReferenceRenderGraphResources
	{
		RGTextureId m_PreviousSum;
		RGTextureId m_NextSum;
		// False for the first sample, whose previous sum is never read.
		bool m_PreviousValid = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_PreviousSum.IsValid() && m_NextSum.IsValid();
		}
	};

	[[nodiscard]] constexpr float TemporalReferenceRadicalInverse(
		uint32_t index, uint32_t base) noexcept
	{
		float result = 0.0f;
		float scale = 1.0f / static_cast<float>(base);
		for (; index > 0; index /= base)
		{
			result += static_cast<float>(index % base) * scale;
			scale /= static_cast<float>(base);
		}
		return result;
	}

	// Halton(2, 3) phase index + 1 in pixels, centered on the pixel. The first eight
	// phases equal the production Temporal AA sequence. Uniformly distributed phases with
	// point sampling realize a one-pixel box reconstruction filter.
	[[nodiscard]] constexpr Vector2 GetTemporalReferenceJitterPixels(uint32_t index) noexcept
	{
		return Vector2(TemporalReferenceRadicalInverse(index + 1, 2) - 0.5f,
			TemporalReferenceRadicalInverse(index + 1, 3) - 0.5f);
	}
}
