#pragma once
#include "GGLabRuntime/Graphics/RenderQueue.h"

#include <array>
#include <optional>
#include <string>

namespace gglab
{
	enum class DepthCoverageExecutionMode : uint8_t
	{
		DepthPrepassEqual,
		ForwardDepthWrite,
		SkipGeometry,
	};

	struct DepthCoverageFramePlan
	{
		DepthCoverageExecutionMode m_ExecutionMode = DepthCoverageExecutionMode::SkipGeometry;
		const RenderQueue* m_SourceRenderQueue = nullptr;
		const DepthCoverageRasterDomain* m_RasterDomain = nullptr;
		std::string m_Diagnostic;
		bool m_HasDepthCoverageDraws = false;
		bool m_HasTransparentDraws = false;

		[[nodiscard]] bool UsesDepthPrepassEqual() const noexcept
		{
			return m_ExecutionMode == DepthCoverageExecutionMode::DepthPrepassEqual;
		}

		[[nodiscard]] bool UsesForwardDepthWrite() const noexcept
		{
			return m_ExecutionMode == DepthCoverageExecutionMode::ForwardDepthWrite;
		}

		[[nodiscard]] bool RendersGeometry() const noexcept
		{
			return m_ExecutionMode != DepthCoverageExecutionMode::SkipGeometry;
		}

		[[nodiscard]] bool AddsForwardOpaquePass() const noexcept
		{
			return RendersGeometry() && m_HasDepthCoverageDraws;
		}

		[[nodiscard]] bool AddsForwardTransparentPass() const noexcept
		{
			return RendersGeometry() && m_HasTransparentDraws;
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
