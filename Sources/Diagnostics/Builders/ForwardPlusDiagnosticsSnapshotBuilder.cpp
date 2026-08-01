#include "Core/Precompiled.h"
#include "Diagnostics/Builders/ForwardPlusDiagnosticsSnapshotBuilder.h"

#include "Diagnostics/Snapshots/ForwardPlusDiagnosticsSnapshot.h"
#include "Graphics/Pipeline/ForwardPlusDebugReadback.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPass/ForwardPlusGraphResources.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	namespace
	{
		uint32_t DivideRoundUp(uint32_t value, uint32_t divisor) noexcept
		{
			return divisor > 0 ? (value + divisor - 1) / divisor : 0;
		}
	}

	ForwardPlusDiagnosticsSnapshot BuildForwardPlusDiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph) noexcept
	{
		ForwardPlusDiagnosticsSnapshot snapshot{};
		const auto* resources = renderGraph.GetBlackboard().TryGet<RGForwardPlusResources>(
			ForwardPlusResourcesName);
		if (!resources)
		{
			return snapshot;
		}

		snapshot.m_Available = true;
		snapshot.m_Status = resources->m_Status;
		snapshot.m_TileGrid = resources->m_TileGrid;
		snapshot.m_DirectionalLightCount = resources->m_DirectionalLightCount;
		snapshot.m_LocalLightCount = resources->m_LocalLightCount;
		snapshot.m_ActiveLightTestLaneCount =
			std::min(resources->m_LocalLightCount, ForwardPlusTileLightCapacity);
		snapshot.m_ActiveLightTestLaneRatio =
			static_cast<double>(snapshot.m_ActiveLightTestLaneCount) /
			static_cast<double>(ForwardPlusTileLightCapacity);

		if (resources->m_TileGrid.IsValid())
		{
			snapshot.m_HeaderLogicalBytes =
				static_cast<uint64_t>(resources->m_TileGrid.m_TileCount) *
				sizeof(ForwardPlusTileHeader);
			snapshot.m_IndexLogicalBytes =
				static_cast<uint64_t>(resources->m_TileGrid.m_TileCount) *
				ForwardPlusTileLightCapacity * sizeof(uint32_t);
			if (resources->m_TileDepthRanges.IsValid())
			{
				snapshot.m_DepthRangeLogicalBytes =
					static_cast<uint64_t>(resources->m_TileGrid.m_TileCount) *
					sizeof(ForwardPlusTileDepthRange);
			}
		}

		if (const RHIDevice* device = renderer.GetDevice())
		{
			const RHIShaderWaveCapabilities waveCapabilities =
				device->GetShaderWaveCapabilities();
			if (waveCapabilities.IsValid())
			{
				snapshot.m_MinWaveLaneCount = waveCapabilities.m_MinLaneCount;
				snapshot.m_MaxWaveLaneCount = waveCapabilities.m_MaxLaneCount;
				snapshot.m_MinTheoreticalWavesPerGroup = DivideRoundUp(
					ForwardPlusCullThreadCount, waveCapabilities.m_MaxLaneCount);
				snapshot.m_MaxTheoreticalWavesPerGroup = DivideRoundUp(
					ForwardPlusCullThreadCount, waveCapabilities.m_MinLaneCount);
			}
		}

		const GpuProfiler* gpuProfiler = renderer.GetGpuProfiler();
		snapshot.m_GpuProfilerEnabled = gpuProfiler && gpuProfiler->IsEnabled();
		if (gpuProfiler)
		{
			const GpuProfileFrameSnapshot gpuFrame = gpuProfiler->GetLatestFrame();
			snapshot.m_GpuTimingAvailable = gpuFrame.IsValid();
			snapshot.m_GpuFrameIndex = gpuFrame.m_FrameIndex;
			for (const auto& sample : gpuFrame.m_Samples)
			{
				if (sample.m_Name == "Lighting.ForwardPlus.Cull")
				{
					snapshot.m_CurrentCullGpuMilliseconds += sample.m_Milliseconds;
				}
				else if (sample.m_Name == "Geometry.ForwardOpaque")
				{
					snapshot.m_CurrentOpaqueGpuMilliseconds += sample.m_Milliseconds;
				}
			}
		}

		const auto& debugReadback = resources->m_DebugReadback;
		if (!debugReadback)
		{
			return snapshot;
		}

		const uint64_t currentGeneration = debugReadback->GetCurrentGeneration();
		const ForwardPlusPerformanceReadback performance = debugReadback->GetPerformance();
		snapshot.m_LegacyOpaqueGpuMilliseconds = performance.m_LegacyOpaqueMilliseconds;
		snapshot.m_ForwardPlusCullGpuMilliseconds =
			performance.m_ForwardPlusCullMilliseconds;
		snapshot.m_ForwardPlusOpaqueGpuMilliseconds =
			performance.m_ForwardPlusOpaqueMilliseconds;
		snapshot.m_PerformanceSamplePairAvailable =
			performance.m_HasLegacySample && performance.m_HasForwardPlusSample;
		if (snapshot.m_PerformanceSamplePairAvailable)
		{
			snapshot.m_LatestForwardPlusSampleLower =
				performance.m_ForwardPlusCullMilliseconds +
				performance.m_ForwardPlusOpaqueMilliseconds <
				performance.m_LegacyOpaqueMilliseconds;
		}

		const ForwardPlusTileReadback selected = debugReadback->GetLatest();
		if (selected.m_IsValid && IsForwardPlusReadbackGenerationCurrent(
			selected.m_RequestGeneration, currentGeneration) &&
			selected.m_TileGrid.m_Width == resources->m_TileGrid.m_Width &&
			selected.m_TileGrid.m_Height == resources->m_TileGrid.m_Height)
		{
			snapshot.m_SelectedTileAvailable = true;
			snapshot.m_SelectedTileX = selected.m_TileX;
			snapshot.m_SelectedTileY = selected.m_TileY;
			snapshot.m_SelectedHeader = selected.m_Header;
			const uint32_t selectedLightCount =
				std::min(selected.m_Header.GetCount(), ForwardPlusTileLightCapacity);
			snapshot.m_SelectedLights.reserve(selectedLightCount);
			for (uint32_t lightOffset = 0; lightOffset < selectedLightCount; ++lightOffset)
			{
				const uint32_t globalLightIndex = selected.m_LightIndices[lightOffset];
				const uint32_t lightTableIndex = globalLightIndex >= resources->m_LightBaseIndex
					? globalLightIndex - resources->m_LightBaseIndex
					: ForwardPlusTileLightCapacity;
				snapshot.m_SelectedLights.push_back({
					.m_GlobalLightIndex = globalLightIndex,
					.m_LightType = lightTableIndex < resources->m_LightTypesByIndex.size()
						? resources->m_LightTypesByIndex[lightTableIndex]
						: std::numeric_limits<uint32_t>::max(),
					});
			}
		}

		const ForwardPlusHdrDiffReadback hdrDiff = debugReadback->GetLatestHdrDiff();
		if (hdrDiff.m_IsValid && IsForwardPlusReadbackGenerationCurrent(
			hdrDiff.m_RequestGeneration, currentGeneration) &&
			hdrDiff.m_Width == resources->m_TileGrid.m_Width &&
			hdrDiff.m_Height == resources->m_TileGrid.m_Height)
		{
			snapshot.m_HdrDiffAvailable = true;
			snapshot.m_MaxAbsoluteHdrError = hdrDiff.m_MaxAbsoluteError;
			snapshot.m_MaxRelativeLuminanceError =
				hdrDiff.m_MaxRelativeLuminanceError;
			snapshot.m_MaxErrorPixelX = hdrDiff.m_MaxErrorPixelX;
			snapshot.m_MaxErrorPixelY = hdrDiff.m_MaxErrorPixelY;
			snapshot.m_ComparedPixelCount = hdrDiff.m_ComparedPixelCount;
			snapshot.m_HdrDiffWithinTolerance =
				IsForwardPlusHdrDiffWithinTolerance(hdrDiff);
		}

		const std::shared_ptr<const ForwardPlusGridReadback> grid =
			debugReadback->GetLatestGrid();
		if (!grid || !grid->m_IsValid || !IsForwardPlusReadbackGenerationCurrent(
			grid->m_RequestGeneration, currentGeneration) ||
			grid->m_TileGrid.m_Width != resources->m_TileGrid.m_Width ||
			grid->m_TileGrid.m_Height != resources->m_TileGrid.m_Height ||
			grid->m_Headers.size() != resources->m_TileGrid.m_TileCount ||
			grid->m_DepthRanges.size() != resources->m_TileGrid.m_TileCount)
		{
			return snapshot;
		}

		snapshot.m_GridReadbackAvailable = true;
		snapshot.m_ReadbackFrameSerial = grid->m_FrameSerial;
		snapshot.m_ReadbackGeneration = grid->m_RequestGeneration;
		const ForwardPlusGridMetrics metrics = BuildForwardPlusGridMetrics(
			grid->m_TileGrid, grid->m_Headers, grid->m_DepthRanges);
		if (!metrics.m_IsValid)
		{
			snapshot.m_GridReadbackAvailable = false;
			return snapshot;
		}
		snapshot.m_NonEmptyLightListTileCount = metrics.m_NonEmptyLightListTileCount;
		snapshot.m_EmptyLightListTileCount = metrics.m_EmptyLightListTileCount;
		snapshot.m_TotalLightReferences = metrics.m_TotalLightReferences;
		snapshot.m_AverageLightsPerTile = metrics.m_AverageLightsPerTile;
		snapshot.m_MaxLightsPerTile = metrics.m_MaxLightsPerTile;
		snapshot.m_OverflowTileCount = metrics.m_OverflowTileCount;
		snapshot.m_MinViewZ = metrics.m_MinViewZ;
		snapshot.m_MaxViewZ = metrics.m_MaxViewZ;
		snapshot.m_Tiles.reserve(grid->m_TileGrid.m_TileCount);
		for (uint32_t tileIndex = 0; tileIndex < grid->m_TileGrid.m_TileCount; ++tileIndex)
		{
			const ForwardPlusTileHeader& header = grid->m_Headers[tileIndex];
			const ForwardPlusTileDepthRange& depthRange = grid->m_DepthRanges[tileIndex];
			const uint32_t lightCount = header.GetCount();
			snapshot.m_Tiles.push_back({
				.m_LightCount = lightCount,
				.m_MinViewZ = depthRange.m_MinViewZ,
				.m_MaxViewZ = depthRange.m_MaxViewZ,
				.m_HasGeometry = depthRange.IsValid(),
				});
		}
		return snapshot;
	}
}
