#pragma once

#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/PostProcess/PostProcessDebug.h"
#include "Graphics/RenderGraph/RGResource.h"

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
			return resources.m_ReprojectionDiagnostics;
		default:
			return {};
		}
	}
}
