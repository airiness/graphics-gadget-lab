#pragma once

#include "GGLabRuntime/Graphics/Pipeline/TemporalHistoryTypes.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessDebug.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <cstdint>

namespace gglab
{
	inline constexpr const char* TemporalAAResourcesName = "TAA.Resources";

	struct RGTemporalAAResources
	{
		TemporalHistoryRenderGraphResources m_History{};
		RGTextureId m_ResolvedSceneColor{};
		// The selected TAA preview payload. Normally RGBA = history weight,
		// rejection reason, previous U, previous V. TemporalHistoryColor carries
		// current accumulated color; TemporalHistoryAge carries normalized NextAge.
		RGTextureId m_ReprojectionDiagnostics{};
		// Display-extent raw depth of the nearest render sample, resolved when the render
		// extent is smaller; it becomes the post-temporal display depth.
		RGTextureId m_DisplayDepthSource{};
		// Display extent of the resolve output.
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_History.IsValid() && m_ResolvedSceneColor.IsValid() &&
				m_ReprojectionDiagnostics.IsValid() && m_Width > 0 && m_Height > 0;
		}
	};

	[[nodiscard]] constexpr bool UsesTemporalAAHistoryColorPreviewPayload(
		PostProcessDebugTap tap) noexcept
	{
		return tap == PostProcessDebugTap::TemporalHistoryColor;
	}

	[[nodiscard]] constexpr bool UsesTemporalAAHistoryAgePreviewPayload(
		PostProcessDebugTap tap) noexcept
	{
		return tap == PostProcessDebugTap::TemporalHistoryAge;
	}

	[[nodiscard]] constexpr bool UsesTemporalAAClipDistancePreviewPayload(
		PostProcessDebugTap tap) noexcept
	{
		return tap == PostProcessDebugTap::TemporalClipDistance;
	}

	// Taps read from the Temporal AA diagnostics texture.
	[[nodiscard]] constexpr bool IsTemporalAADiagnosticsTap(PostProcessDebugTap tap) noexcept
	{
		switch (tap)
		{
		case PostProcessDebugTap::TemporalHistoryColor:
		case PostProcessDebugTap::TemporalReprojectionUV:
		case PostProcessDebugTap::TemporalRejection:
		case PostProcessDebugTap::TemporalHistoryWeight:
		case PostProcessDebugTap::TemporalHistoryAge:
		case PostProcessDebugTap::TemporalClipDistance:
			return true;
		default:
			return false;
		}
	}

	[[nodiscard]] inline RGTextureId ResolveTemporalAAPreviewSource(
		const RGTemporalAAResources& resources, PostProcessDebugTap tap) noexcept
	{
		switch (tap)
		{
		case PostProcessDebugTap::TemporalHistoryColor:
		case PostProcessDebugTap::TemporalReprojectionUV:
		case PostProcessDebugTap::TemporalRejection:
		case PostProcessDebugTap::TemporalHistoryWeight:
		case PostProcessDebugTap::TemporalHistoryAge:
		case PostProcessDebugTap::TemporalClipDistance:
			return resources.m_ReprojectionDiagnostics;
		default:
			return {};
		}
	}
}
