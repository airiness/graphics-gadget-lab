#pragma once

#include "Diagnostics/SnapshotCommon.h"
#include "Graphics/Pipeline/ForwardPlus.h"

#include <vector>

namespace gglab
{
	struct ForwardPlusTileDiagnostics
	{
		uint32_t m_LightCount = 0;
		float m_MinViewZ = 0.0f;
		float m_MaxViewZ = 0.0f;
		bool m_HasGeometry = false;
	};

	struct ForwardPlusSelectedLightDiagnostics
	{
		uint32_t m_GlobalLightIndex = 0;
		uint32_t m_LightType = 0;
	};

	struct ForwardPlusDiagnosticsSnapshot
	{
		ForwardPlusFrameStatus m_Status = ForwardPlusFrameStatus::Disabled;
		ForwardPlusTileGrid m_TileGrid{};
		uint64_t m_ReadbackFrameSerial = 0;
		uint64_t m_ReadbackGeneration = 0;

		uint32_t m_DirectionalLightCount = 0;
		uint32_t m_LocalLightCount = 0;
		uint32_t m_NonEmptyLightListTileCount = 0;
		uint32_t m_EmptyLightListTileCount = 0;
		uint64_t m_TotalLightReferences = 0;
		double m_AverageLightsPerTile = 0.0;
		uint32_t m_MaxLightsPerTile = 0;
		uint32_t m_OverflowTileCount = 0;
		float m_MinViewZ = 0.0f;
		float m_MaxViewZ = 0.0f;

		uint32_t m_ThreadGroupSize = ForwardPlusCullThreadCount;
		uint32_t m_MinWaveLaneCount = 0;
		uint32_t m_MaxWaveLaneCount = 0;
		uint32_t m_MinTheoreticalWavesPerGroup = 0;
		uint32_t m_MaxTheoreticalWavesPerGroup = 0;
		uint32_t m_ActiveLightTestLaneCount = 0;
		double m_ActiveLightTestLaneRatio = 0.0;

		uint64_t m_HeaderLogicalBytes = 0;
		uint64_t m_IndexLogicalBytes = 0;
		uint64_t m_DepthRangeLogicalBytes = 0;

		uint32_t m_SelectedTileX = 0;
		uint32_t m_SelectedTileY = 0;
		ForwardPlusTileHeader m_SelectedHeader{};
		std::vector<ForwardPlusSelectedLightDiagnostics> m_SelectedLights;
		std::vector<ForwardPlusTileDiagnostics> m_Tiles;

		float m_MaxAbsoluteHdrError = 0.0f;
		float m_MaxRelativeLuminanceError = 0.0f;
		uint32_t m_MaxErrorPixelX = 0;
		uint32_t m_MaxErrorPixelY = 0;
		uint32_t m_ComparedPixelCount = 0;

		uint64_t m_GpuFrameIndex = 0;
		double m_CurrentCullGpuMilliseconds = 0.0;
		double m_CurrentOpaqueGpuMilliseconds = 0.0;
		double m_LegacyOpaqueGpuMilliseconds = 0.0;
		double m_ForwardPlusCullGpuMilliseconds = 0.0;
		double m_ForwardPlusOpaqueGpuMilliseconds = 0.0;

		bool m_Available = false;
		bool m_GridReadbackAvailable = false;
		bool m_SelectedTileAvailable = false;
		bool m_HdrDiffAvailable = false;
		bool m_HdrDiffWithinTolerance = false;
		bool m_GpuProfilerEnabled = false;
		bool m_GpuTimingAvailable = false;
		bool m_PerformanceSamplePairAvailable = false;
		bool m_LatestForwardPlusSampleLower = false;
	};

	template <> struct SnapshotTraits<ForwardPlusDiagnosticsSnapshot>
	{
		static constexpr SnapshotId Id =
			MakeSnapshotId("Diagnostics.ForwardPlusDiagnosticsSnapshot");
	};
}
