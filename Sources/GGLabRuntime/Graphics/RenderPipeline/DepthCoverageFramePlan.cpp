#include "Graphics/RenderPipeline/DepthCoverageFramePlan.h"

#include <format>
#include <memory>
#include <string>
#include <utility>

namespace gglab
{
	namespace
	{
		void RejectGeometry(DepthCoverageFramePlan& plan, std::string diagnostic)
		{
			plan.m_ExecutionMode = DepthCoverageExecutionMode::SkipGeometry;
			plan.m_Diagnostic = std::move(diagnostic);
		}

	}

	DepthCoverageFramePlan BuildDepthCoverageFramePlan(
		const DepthCoverageFramePlanBuildInfo& buildInfo)
	{
		DepthCoverageFramePlan plan{
			.m_ExecutionMode = DepthCoverageExecutionMode::DepthPrepassEqual,
			.m_SourceRenderQueue = buildInfo.m_RenderQueue,
			.m_RasterDomain = buildInfo.m_RenderQueue
								  ? std::addressof(buildInfo.m_RenderQueue->m_CoverageRasterDomain)
								  : nullptr,
		};

		if (!buildInfo.m_RenderQueue)
		{
			RejectGeometry(plan, "Depth coverage frame planning requires a RenderQueue.");
			return plan;
		}

		const RenderQueue& renderQueue = *buildInfo.m_RenderQueue;
		const DepthCoverageRasterDomain& rasterDomain = renderQueue.m_CoverageRasterDomain;
		if (buildInfo.m_DepthConvention != DepthConvention::Reversed)
		{
			RejectGeometry(
				plan, "Depth coverage frame planning only supports the Reversed-Z Forward path.");
			return plan;
		}
		if (renderQueue.m_ViewId != buildInfo.m_ExpectedViewId)
		{
			RejectGeometry(plan, "RenderQueue view does not match the display view.");
			return plan;
		}
		if (!rasterDomain.IsValid() ||
			!rasterDomain.MatchesTargetExtent(buildInfo.m_TargetWidth, buildInfo.m_TargetHeight) ||
			rasterDomain.m_DepthConvention != buildInfo.m_DepthConvention)
		{
			RejectGeometry(
				plan, "Raster domain does not match the frame view, extent, or depth convention.");
			return plan;
		}

		bool requiresForwardDepthWrite = false;
		uint32_t expectedStart = 0;
		for (size_t bucketIndex = 0; bucketIndex < utils::ToIndex(RenderBucket::Count);
			++bucketIndex)
		{
			const auto bucket = static_cast<RenderBucket>(bucketIndex);
			const DrawItemsRange range = renderQueue.m_BucketDrawRanges[bucketIndex];
			if (range.m_Start != expectedStart || range.m_Start > renderQueue.m_DrawItems.size() ||
				range.m_Count > renderQueue.m_DrawItems.size() - range.m_Start)
			{
				RejectGeometry(
					plan, std::format("RenderQueue range {} is not a contiguous in-bounds range.",
						bucketIndex));
				return plan;
			}

			plan.m_HasDepthCoverageDraws |=
				range.m_Count > 0 &&
				(bucket == RenderBucket::Opaque || bucket == RenderBucket::AlphaTest);
			plan.m_HasTransparentDraws |= range.m_Count > 0 && bucket == RenderBucket::Transparent;

			for (uint32_t offset = 0; offset < range.m_Count; ++offset)
			{
				const uint32_t drawItemIndex = range.m_Start + offset;
				const DrawItem& drawItem = renderQueue.m_DrawItems[drawItemIndex];
				if ((drawItem.m_VariantBits & ~RenderQueueBuilder::VariantMask) != 0)
				{
					RejectGeometry(
						plan, std::format("RenderQueue item {} has unsupported variant bits.",
							drawItemIndex));
					return plan;
				}
				if (drawItem.m_Bucket != bucket ||
					RenderQueueBuilder::DecodeVariantBucket(drawItem.m_VariantBits) != bucket)
				{
					RejectGeometry(
						plan, std::format("RenderQueue item {} disagrees with bucket {}.",
							drawItemIndex, bucketIndex));
					return plan;
				}
				if (!drawItem.m_CoverageDrawPacket.IsValid())
				{
					RejectGeometry(
						plan, std::format("RenderQueue item {} has an invalid draw packet.",
							drawItemIndex));
					return plan;
				}
				if (bucket == RenderBucket::Transparent)
				{
					continue;
				}

				const size_t variantIndex =
					static_cast<size_t>(drawItem.m_VariantBits & RenderQueueBuilder::VariantMask);
				const auto& prepassSignature = buildInfo.m_PrepassPipelineSignatures[variantIndex];
				const auto& forwardSignature = buildInfo.m_ForwardPipelineSignatures[variantIndex];
				if (!prepassSignature || !forwardSignature)
				{
					requiresForwardDepthWrite = true;
					if (plan.m_Diagnostic.empty())
					{
						plan.m_Diagnostic = std::format(
							"Coverage variant {} did not publish both pipeline signatures.",
							variantIndex);
					}
					continue;
				}
				if (!prepassSignature->IsValid() || !forwardSignature->IsValid())
				{
					RejectGeometry(plan,
						std::format("Coverage variant {} published an invalid pipeline signature.",
							variantIndex));
					return plan;
				}

				const DepthCoverageValidationResult comparison =
					CompareDepthCoveragePipelineSignatures(*prepassSignature, *forwardSignature);
				if (!comparison.m_Matches)
				{
					requiresForwardDepthWrite = true;
					if (plan.m_Diagnostic.empty())
					{
						plan.m_Diagnostic = std::format("Coverage variant {} mismatch: {}.",
							variantIndex, comparison.m_Mismatch);
					}
				}
			}
			expectedStart += range.m_Count;
		}

		if (expectedStart != renderQueue.m_DrawItems.size())
		{
			RejectGeometry(plan, "RenderQueue ranges do not cover every draw item exactly once.");
			return plan;
		}
		if (requiresForwardDepthWrite)
		{
			plan.m_ExecutionMode = DepthCoverageExecutionMode::ForwardDepthWrite;
		}

		return plan;
	}
}
