#include "Diagnostics/Builders/PostProcessDiagnosticsSnapshotBuilder.h"
#include "Diagnostics/Snapshots/PostProcessDiagnosticsSnapshot.h"
#include "Graphics/PostProcess/PostProcessGraphResources.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderPass/SceneDepthGraphResources.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

namespace gglab
{
	namespace
	{
		PostProcessTextureDiagnostics BuildTextureDiagnostics(
			const RenderGraph& renderGraph, const RGPostProcessColor& color) noexcept
		{
			PostProcessTextureDiagnostics diagnostics{};
			if (!color.m_Texture.IsValid())
			{
				return diagnostics;
			}

			const RHITextureDesc& desc = renderGraph.GetTextureDesc(color.m_Texture);
			diagnostics.m_Width = static_cast<uint32_t>(desc.m_Extent.m_Width);
			diagnostics.m_Height = desc.m_Extent.m_Height;
			diagnostics.m_Format = desc.m_Format;
			diagnostics.m_ColorState = color.m_State;
			diagnostics.m_PreExposure = color.m_PreExposure;
			diagnostics.m_LogicalBytes = desc.m_Extent.m_Width * desc.m_Extent.m_Height *
				desc.m_Extent.m_Depth * desc.m_ArraySize *
				GetRHIFormatInfo(desc.m_Format).m_BytesPerBlock;
			diagnostics.m_Available = true;
			return diagnostics;
		}
	}

	PostProcessDiagnosticsSnapshot BuildPostProcessDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept
	{
		PostProcessDiagnosticsSnapshot snapshot{};
		const auto* resources =
			renderGraph.GetBlackboard().TryGet<RGPostProcessResources>(PostProcessResourcesName);
		if (resources)
		{
			snapshot.m_SceneColor =
				BuildTextureDiagnostics(renderGraph, resources->m_Inputs.m_SceneColor);
			snapshot.m_BloomPrefilter =
				BuildTextureDiagnostics(renderGraph, resources->m_Bloom.m_Prefilter);
			snapshot.m_BloomLevelCount = resources->m_Bloom.m_LevelCount;
			for (uint32_t level = 0; level < resources->m_Bloom.m_LevelCount; ++level)
			{
				auto& levelDiagnostics = snapshot.m_BloomPyramid[level];
				levelDiagnostics = BuildTextureDiagnostics(
					renderGraph, resources->m_Bloom.m_DownsampledPyramid[level]);
				snapshot.m_BloomLogicalBytes += levelDiagnostics.m_LogicalBytes;
			}
			snapshot.m_BloomResult =
				BuildTextureDiagnostics(renderGraph, resources->m_Bloom.m_Result);
		}
		const auto* sceneDepth =
			renderGraph.GetBlackboard().TryGet<RGSceneDepthResources>(SceneDepthResourcesName);
		if (sceneDepth && sceneDepth->m_Texture.IsValid())
		{
			const auto& desc = renderGraph.GetTextureDesc(sceneDepth->m_Texture);
			snapshot.m_SceneDepth = {
				.m_Width = static_cast<uint32_t>(desc.m_Extent.m_Width),
				.m_Height = desc.m_Extent.m_Height,
				.m_ResourceFormat = desc.m_Format,
				.m_DsvFormat = sceneDepth->m_DsvDesc.m_Format,
				.m_SrvFormat = sceneDepth->m_SrvDesc.m_Format,
				.m_ClearDepth = desc.m_ClearValue ? desc.m_ClearValue->m_Depth : 0.0f,
				.m_Convention = sceneDepth->m_Convention,
				.m_HasTypedClear = desc.m_ClearValue.has_value(),
				.m_Available = true,
			};
		}

		const auto* registry = renderer.GetRenderResourceRegistry();
		if (registry)
		{
			snapshot.m_Preview = registry->GetPostProcessPreviewDiagnostics();
		}

		const auto* gpuProfiler = renderer.GetGpuProfiler();
		snapshot.m_GpuProfilerEnabled = gpuProfiler && gpuProfiler->IsEnabled();
		if (gpuProfiler)
		{
			const auto gpuFrame = gpuProfiler->GetLatestFrame();
			snapshot.m_GpuTimingAvailable = gpuFrame.IsValid();
			snapshot.m_GpuFrameIndex = gpuFrame.m_FrameIndex;
			for (const auto& sample : gpuFrame.m_Samples)
			{
				if (!sample.m_Name.starts_with("PostProcess."))
				{
					continue;
				}
				snapshot.m_GpuPasses.push_back({
					.m_Name = sample.m_Name,
					.m_Milliseconds = sample.m_Milliseconds,
					.m_CallCount = sample.m_CallCount,
					});
				snapshot.m_PostProcessGpuMilliseconds += sample.m_Milliseconds;
				if (sample.m_Name.starts_with("PostProcess.Bloom."))
				{
					snapshot.m_BloomGpuMilliseconds += sample.m_Milliseconds;
				}
			}
		}
		return snapshot;
	}
}
