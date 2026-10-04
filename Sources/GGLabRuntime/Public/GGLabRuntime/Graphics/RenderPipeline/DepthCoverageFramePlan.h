#pragma once
#include "GGLabRuntime/Graphics/RenderQueue.h"

#include <array>
#include <optional>
#include <string>

namespace gglab
{
	// The Forward pipeline draws every coverage item with a depth prepass followed by a
	// depth-equal Forward pass. A plan that cannot satisfy that contract is invalid and
	// carries the first violation in m_Diagnostic; it never selects another topology.
	struct DepthCoverageFramePlan
	{
		const RenderQueue* m_SourceRenderQueue = nullptr;
		const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		std::string m_Diagnostic;
		bool m_IsValid = false;
		bool m_HasDepthCoverageDraws = false;
		bool m_HasTransparentDraws = false;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_IsValid;
		}

		[[nodiscard]] bool AddsForwardOpaquePass() const noexcept
		{
			return m_IsValid && m_HasDepthCoverageDraws;
		}

		[[nodiscard]] bool AddsForwardTransparentPass() const noexcept
		{
			return m_IsValid && m_HasTransparentDraws;
		}
	};

	struct DepthCoverageFramePlanBuildInfo
	{
		const RenderQueue* m_RenderQueue = nullptr;
		RenderViewID m_ExpectedViewId = RenderViewID::Unknown;
		uint32_t m_TargetWidth = 0;
		uint32_t m_TargetHeight = 0;
		DepthConvention m_DepthConvention = DepthConvention::Reversed;
		std::array<std::optional<DepthCoveragePipelineSignature>, RenderQueueBuilder::VariantCount>
			m_PrepassPipelineSignatures{};
		std::array<std::optional<DepthCoveragePipelineSignature>, RenderQueueBuilder::VariantCount>
			m_ForwardPipelineSignatures{};
	};

	[[nodiscard]] DepthCoverageFramePlan BuildDepthCoverageFramePlan(
		const DepthCoverageFramePlanBuildInfo& buildInfo);
}
