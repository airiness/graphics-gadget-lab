#include "Core/Precompiled.h"
#include "Graphics/RenderPipeline/DepthCoverageFramePlan.h"

namespace gglab
{
	namespace
	{
		void RejectGeometry(
			DepthCoverageFramePlan& plan,
			std::string diagnostic)
		{
			plan.m_ExecutionMode =
				DepthCoverageExecutionMode::SkipGeometry;
			plan.m_Diagnostic = std::move(diagnostic);
		}

		[[nodiscard]] bool ValidateRenderQueueStructure(
			const RenderQueue& renderQueue,
			std::string& diagnostic)
		{
			uint32_t expectedStart = 0;
			for (size_t bucketIndex = 0;
				bucketIndex < utils::ToIndex(RenderBucket::Count);
				++bucketIndex)
			{
				const auto bucket =
					static_cast<RenderBucket>(bucketIndex);
				const DrawItemsRange range =
					renderQueue.m_BucketDrawRanges[bucketIndex];
				if (range.m_Start != expectedStart ||
					range.m_Start > renderQueue.m_DrawItems.size() ||
					range.m_Count >
						renderQueue.m_DrawItems.size() -
							range.m_Start)
				{
					diagnostic = std::format(
						"RenderQueue range {} is not a contiguous in-bounds range.",
						bucketIndex);
					return false;
				}

				for (uint32_t offset = 0;
					offset < range.m_Count;
					++offset)
				{
					const DrawItem& drawItem =
						renderQueue.m_DrawItems[
							range.m_Start + offset];
					if (drawItem.m_Bucket != bucket ||
						RenderQueueBuilder::DecodeVariantBucket(
							drawItem.m_VariantBits) != bucket)
					{
						diagnostic = std::format(
							"RenderQueue item {} disagrees with bucket {}.",
							range.m_Start + offset,
							bucketIndex);
						return false;
					}
					if ((drawItem.m_VariantBits &
						~RenderQueueBuilder::VariantMask) != 0)
					{
						diagnostic = std::format(
							"RenderQueue item {} has unsupported variant bits.",
							range.m_Start + offset);
						return false;
					}
					if (!drawItem.m_CoverageDrawPacket.IsValid())
					{
						diagnostic = std::format(
							"RenderQueue item {} has an invalid draw packet.",
							range.m_Start + offset);
						return false;
					}
				}
				expectedStart += range.m_Count;
			}

			if (expectedStart != renderQueue.m_DrawItems.size())
			{
				diagnostic =
					"RenderQueue ranges do not cover every draw item exactly once.";
				return false;
			}
			return true;
		}
	}

	DepthCoverageFramePlan BuildDepthCoverageFramePlan(
		const DepthCoverageFramePlanBuildInfo& buildInfo)
	{
		DepthCoverageFramePlan plan{
			.m_ExecutionMode =
				DepthCoverageExecutionMode::DepthPrepassEqual,
			.m_SourceRenderQueue = buildInfo.m_RenderQueue,
			.m_RasterDomain = buildInfo.m_RenderQueue ?
				std::addressof(
					buildInfo.m_RenderQueue->
						m_CoverageRasterDomain) :
				nullptr,
		};

		if (!buildInfo.m_RenderQueue)
		{
			RejectGeometry(
				plan,
				"Depth coverage frame planning requires a RenderQueue.");
			return plan;
		}

		const RenderQueue& renderQueue =
			*buildInfo.m_RenderQueue;
		if (renderQueue.m_DrawItems.empty())
		{
			return plan;
		}
		if (!buildInfo.m_ShadingPipelinesAvailable)
		{
			RejectGeometry(
				plan,
				"Forward shading pipelines are unavailable.");
			return plan;
		}

		const DepthCoverageRasterDomain& rasterDomain =
			renderQueue.m_CoverageRasterDomain;
		if (renderQueue.m_ViewId != buildInfo.m_ExpectedViewId)
		{
			RejectGeometry(
				plan,
				"RenderQueue view does not match the display view.");
			return plan;
		}
		if (!rasterDomain.IsValid() ||
			!rasterDomain.MatchesTargetExtent(
				buildInfo.m_TargetWidth,
				buildInfo.m_TargetHeight) ||
			rasterDomain.m_DepthConvention !=
				buildInfo.m_DepthConvention)
		{
			RejectGeometry(
				plan,
				"Raster domain does not match the frame view, extent, or depth convention.");
			return plan;
		}

		std::string queueDiagnostic;
		if (!ValidateRenderQueueStructure(
			renderQueue,
			queueDiagnostic))
		{
			RejectGeometry(
				plan,
				std::move(queueDiagnostic));
			return plan;
		}

		const auto& ranges =
			renderQueue.m_BucketDrawRanges;
		for (const RenderBucket bucket :
			{ RenderBucket::Opaque, RenderBucket::AlphaTest })
		{
			const DrawItemsRange range =
				ranges[utils::ToIndex(bucket)];
			for (uint32_t offset = 0;
				offset < range.m_Count;
				++offset)
			{
				const DrawItem& drawItem =
					renderQueue.m_DrawItems[
						range.m_Start + offset];
				const size_t variantIndex =
					static_cast<size_t>(
						drawItem.m_VariantBits &
							RenderQueueBuilder::VariantMask);
				const auto& prepassSignature =
					buildInfo.m_PrepassPipelineSignatures[
						variantIndex];
				const auto& forwardSignature =
					buildInfo.m_ForwardPipelineSignatures[
						variantIndex];
				if (!prepassSignature || !forwardSignature)
				{
					plan.m_ExecutionMode =
						DepthCoverageExecutionMode::
							ForwardDepthWrite;
					plan.m_Diagnostic = std::format(
						"Coverage variant {} did not publish both pipeline signatures.",
						variantIndex);
					return plan;
				}
				if (!prepassSignature->IsValid() ||
					!forwardSignature->IsValid())
				{
					RejectGeometry(
						plan,
						std::format(
							"Coverage variant {} published an invalid pipeline signature.",
							variantIndex));
					return plan;
				}

				const DepthCoverageValidationResult comparison =
					CompareDepthCoveragePipelineSignatures(
						*prepassSignature,
						*forwardSignature);
				if (!comparison.m_Matches)
				{
					plan.m_ExecutionMode =
						DepthCoverageExecutionMode::
							ForwardDepthWrite;
					plan.m_Diagnostic = std::format(
						"Coverage variant {} mismatch: {}.",
						variantIndex,
						comparison.m_Mismatch);
					return plan;
				}
			}
		}

		return plan;
	}
}
