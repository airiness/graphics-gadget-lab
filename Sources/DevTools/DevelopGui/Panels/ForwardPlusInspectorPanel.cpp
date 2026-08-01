#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/ForwardPlusInspectorPanel.h"

#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/ForwardPlusDiagnosticsSnapshot.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	namespace
	{
		enum class HeatmapMode : int32_t
		{
			LightCount,
			MinViewZ,
			MaxViewZ,
		};

		struct PanelState
		{
			HeatmapMode m_HeatmapMode = HeatmapMode::LightCount;
			bool m_ShowGrid = true;
		};

		const char* GetStatusName(ForwardPlusFrameStatus status) noexcept
		{
			switch (status)
			{
			case ForwardPlusFrameStatus::Disabled:
				return "Disabled (Legacy requested)";
			case ForwardPlusFrameStatus::Active:
				return "Active";
			case ForwardPlusFrameStatus::GlobalLightCapacityExceeded:
				return "Legacy fallback: global-light capacity exceeded";
			case ForwardPlusFrameStatus::DepthCoverageUnavailable:
				return "Legacy fallback: depth coverage unavailable";
			case ForwardPlusFrameStatus::RenderSceneUnavailable:
				return "Legacy fallback: render scene unavailable";
			case ForwardPlusFrameStatus::NoOpaqueDraws:
				return "Idle: no opaque draws";
			}
			return "Unknown";
		}

		const char* GetLightTypeName(uint32_t lightType) noexcept
		{
			switch (static_cast<LightType>(lightType))
			{
			case LightType::Directional:
				return "Directional";
			case LightType::Point:
				return "Point";
			case LightType::Spot:
				return "Spot";
			}
			return "Unknown";
		}

		ImU32 GetHeatmapColor(const ForwardPlusDiagnosticsSnapshot& snapshot,
			const ForwardPlusTileDiagnostics& tile, HeatmapMode mode) noexcept
		{
			if (mode != HeatmapMode::LightCount && !tile.m_HasGeometry)
			{
				return IM_COL32(24, 27, 33, 255);
			}

			float normalized = 0.0f;
			if (mode == HeatmapMode::LightCount)
			{
				normalized = static_cast<float>(tile.m_LightCount) /
					static_cast<float>(std::max(snapshot.m_MaxLightsPerTile, 1u));
			}
			else
			{
				const float value = mode == HeatmapMode::MinViewZ
					? tile.m_MinViewZ
					: tile.m_MaxViewZ;
				const float minimum = std::max(snapshot.m_MinViewZ, 1.0e-4f);
				const float maximum = std::max(snapshot.m_MaxViewZ, minimum);
				const float logRange = std::log(maximum) - std::log(minimum);
				normalized = logRange > 1.0e-6f
					? (std::log(std::max(value, minimum)) - std::log(minimum)) / logRange
					: 0.0f;
			}

			normalized = std::clamp(normalized, 0.0f, 1.0f);
			const ImVec4 color = ImColor::HSV(
				(1.0f - normalized) * 0.66f, 0.82f, normalized > 0.0f ? 0.92f : 0.18f);
			return ImGui::ColorConvertFloat4ToU32(color);
		}

		void DrawHeatmap(
			const ForwardPlusDiagnosticsSnapshot& snapshot, PanelState& state) noexcept
		{
			if (!snapshot.m_GridReadbackAvailable || snapshot.m_Tiles.empty())
			{
				ImGui::TextDisabled(
					"Full-grid heatmap is available in the Forward+ Lab diagnostics path.");
				return;
			}

			const char* heatmapNames[] = { "Light Count", "Min View-Z", "Max View-Z" };
			int heatmapMode = static_cast<int>(state.m_HeatmapMode);
			if (ImGui::Combo("Mode##ForwardPlusHeatmapMode", &heatmapMode, heatmapNames,
				std::size(heatmapNames)))
			{
				state.m_HeatmapMode = static_cast<HeatmapMode>(heatmapMode);
			}
			ImGui::SameLine();
			ImGui::Checkbox("Grid", &state.m_ShowGrid);

			const float width = std::clamp(ImGui::GetContentRegionAvail().x, 128.0f, 900.0f);
			const float aspect = static_cast<float>(snapshot.m_TileGrid.m_Height) /
				static_cast<float>(snapshot.m_TileGrid.m_Width);
			const ImVec2 size(width, std::max(width * aspect, 96.0f));
			const ImVec2 origin = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("ForwardPlusHeatmap", size);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
				IM_COL32(15, 17, 22, 255));

			const float tileWidth = size.x / snapshot.m_TileGrid.m_TileCountX;
			const float tileHeight = size.y / snapshot.m_TileGrid.m_TileCountY;
			for (uint32_t tileY = 0; tileY < snapshot.m_TileGrid.m_TileCountY; ++tileY)
			{
				for (uint32_t tileX = 0; tileX < snapshot.m_TileGrid.m_TileCountX; ++tileX)
				{
					const uint32_t tileIndex = tileY * snapshot.m_TileGrid.m_TileCountX + tileX;
					const ImVec2 minimum(
						origin.x + tileX * tileWidth, origin.y + tileY * tileHeight);
					const ImVec2 maximum(minimum.x + tileWidth, minimum.y + tileHeight);
					drawList->AddRectFilled(minimum, maximum,
						GetHeatmapColor(snapshot, snapshot.m_Tiles[tileIndex], state.m_HeatmapMode));
				}
			}

			if (state.m_ShowGrid && tileWidth >= 3.0f && tileHeight >= 3.0f)
			{
				for (uint32_t tileX = 0; tileX <= snapshot.m_TileGrid.m_TileCountX; ++tileX)
				{
					const float x = origin.x + tileX * tileWidth;
					drawList->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + size.y),
						IM_COL32(255, 255, 255, 28));
				}
				for (uint32_t tileY = 0; tileY <= snapshot.m_TileGrid.m_TileCountY; ++tileY)
				{
					const float y = origin.y + tileY * tileHeight;
					drawList->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + size.x, y),
						IM_COL32(255, 255, 255, 28));
				}
			}

			if (snapshot.m_SelectedTileAvailable)
			{
				const ImVec2 minimum(origin.x + snapshot.m_SelectedTileX * tileWidth,
					origin.y + snapshot.m_SelectedTileY * tileHeight);
				const ImVec2 maximum(minimum.x + tileWidth, minimum.y + tileHeight);
				drawList->AddRect(minimum, maximum, IM_COL32(255, 232, 80, 255), 0.0f, 0, 2.0f);
			}

			if (ImGui::IsItemHovered())
			{
				const ImVec2 mouse = ImGui::GetMousePos();
				const uint32_t tileX = std::min(static_cast<uint32_t>((mouse.x - origin.x) / tileWidth),
					snapshot.m_TileGrid.m_TileCountX - 1);
				const uint32_t tileY = std::min(static_cast<uint32_t>((mouse.y - origin.y) / tileHeight),
					snapshot.m_TileGrid.m_TileCountY - 1);
				const auto& tile = snapshot.m_Tiles[
					tileY * snapshot.m_TileGrid.m_TileCountX + tileX];
				ImGui::BeginTooltip();
				ImGui::Text("Tile (%u, %u)", tileX, tileY);
				ImGui::Text("Lights: %u", tile.m_LightCount);
				if (tile.m_HasGeometry)
				{
					ImGui::Text("View-Z: %.4f - %.4f", tile.m_MinViewZ, tile.m_MaxViewZ);
				}
				else
				{
					ImGui::TextDisabled("Background-only tile");
				}
				ImGui::EndTooltip();
			}
		}
	}

	void ForwardPlusInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer || !context.m_Diagnostics)
		{
			ImGui::TextDisabled("Renderer diagnostics are unavailable.");
			return;
		}
		const auto* snapshot =
			context.m_Diagnostics->GetSnapshot<ForwardPlusDiagnosticsSnapshot>();
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("Forward+ diagnostics are unavailable for the current pipeline.");
			return;
		}

		ImGui::Text("Status: %s", GetStatusName(snapshot->m_Status));
		ImGui::Text("Lights: %u local, %u directional", snapshot->m_LocalLightCount,
			snapshot->m_DirectionalLightCount);
		if (snapshot->m_TileGrid.IsValid())
		{
			ImGui::Text("Grid: %u x %u (%u tiles), tile size %u", snapshot->m_TileGrid.m_TileCountX,
				snapshot->m_TileGrid.m_TileCountY, snapshot->m_TileGrid.m_TileCount,
				ForwardPlusTileSize);
		}

		if (ImGui::CollapsingHeader("Tile Statistics", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Non-empty / empty light lists: %u / %u",
				snapshot->m_NonEmptyLightListTileCount,
				snapshot->m_EmptyLightListTileCount);
			ImGui::Text("References: %llu total, %.2f average, %u max",
				static_cast<unsigned long long>(snapshot->m_TotalLightReferences),
				snapshot->m_AverageLightsPerTile, snapshot->m_MaxLightsPerTile);
			ImGui::Text("Overflow: disabled by fixed-stride-64 encoding / %u observed",
				snapshot->m_OverflowTileCount);
			ImGui::Text("Logical buffers: %.1f KiB headers, %.1f KiB indices, %.1f KiB depth ranges",
				static_cast<double>(snapshot->m_HeaderLogicalBytes) / 1024.0,
				static_cast<double>(snapshot->m_IndexLogicalBytes) / 1024.0,
				static_cast<double>(snapshot->m_DepthRangeLogicalBytes) / 1024.0);
			ImGui::Text("View-Z domain: %.4f - %.4f", snapshot->m_MinViewZ,
				snapshot->m_MaxViewZ);
		}

		if (ImGui::CollapsingHeader("Heatmap", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto& panelState = context.PanelState<PanelState>();
			DrawHeatmap(*snapshot, panelState);
		}

		if (ImGui::CollapsingHeader("Selected Tile", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (!snapshot->m_SelectedTileAvailable)
			{
				ImGui::TextDisabled("Waiting for selected-tile GPU readback...");
			}
			else
			{
				ImGui::Text("Tile (%u, %u): offset %u, count %u", snapshot->m_SelectedTileX,
					snapshot->m_SelectedTileY, snapshot->m_SelectedHeader.m_Offset,
					snapshot->m_SelectedHeader.GetCount());
				for (const auto& light : snapshot->m_SelectedLights)
				{
					ImGui::BulletText("%u - %s", light.m_GlobalLightIndex,
						GetLightTypeName(light.m_LightType));
				}
			}
		}

		if (ImGui::CollapsingHeader("Wave and Lane Model", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Thread group: %u | Wave lanes: %u - %u | Theoretical waves: %u - %u",
				snapshot->m_ThreadGroupSize, snapshot->m_MinWaveLaneCount,
				snapshot->m_MaxWaveLaneCount, snapshot->m_MinTheoreticalWavesPerGroup,
				snapshot->m_MaxTheoreticalWavesPerGroup);
			ImGui::Text("Logical active light-test lanes: %u / %u (%.1f%%)",
				snapshot->m_ActiveLightTestLaneCount, ForwardPlusTileLightCapacity,
				snapshot->m_ActiveLightTestLaneRatio * 100.0);
			ImGui::TextDisabled(
				"Hardware occupancy and throughput require PIX or Nsight; they are not inferred here.");
		}

		if (ImGui::CollapsingHeader("Validation and GPU Timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (auto* gpuProfiler = context.m_Renderer->GetGpuProfiler())
			{
				bool enabled = gpuProfiler->IsEnabled();
				if (ImGui::Checkbox("GPU Profiling", &enabled))
				{
					gpuProfiler->SetEnabled(enabled);
				}
			}
			if (snapshot->m_GpuTimingAvailable)
			{
				ImGui::Text("Current frame %llu: cull %.3f ms, opaque %.3f ms",
					static_cast<unsigned long long>(snapshot->m_GpuFrameIndex),
					snapshot->m_CurrentCullGpuMilliseconds,
					snapshot->m_CurrentOpaqueGpuMilliseconds);
			}
			else
			{
				ImGui::TextDisabled("Waiting for completed GPU timestamps...");
			}

			if (snapshot->m_HdrDiffAvailable)
			{
				ImGui::Text("HDR diff: abs %.8f, relative luminance %.8f, pixel (%u, %u), %u samples",
					snapshot->m_MaxAbsoluteHdrError,
					snapshot->m_MaxRelativeLuminanceError, snapshot->m_MaxErrorPixelX,
					snapshot->m_MaxErrorPixelY, snapshot->m_ComparedPixelCount);
				ImGui::TextColored(snapshot->m_HdrDiffWithinTolerance
					? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
					: ImVec4(0.95f, 0.3f, 0.25f, 1.0f),
					snapshot->m_HdrDiffWithinTolerance ? "HDR diff passed" : "HDR diff failed");
			}
			else
			{
				ImGui::TextDisabled("HDR diff is disabled or awaiting a current-generation result.");
			}

			if (snapshot->m_PerformanceSamplePairAvailable)
			{
				const double forwardPlusTotal = snapshot->m_ForwardPlusCullGpuMilliseconds +
					snapshot->m_ForwardPlusOpaqueGpuMilliseconds;
				ImGui::Text("Legacy opaque %.3f ms | Forward+ cull + opaque %.3f + %.3f = %.3f ms",
					snapshot->m_LegacyOpaqueGpuMilliseconds,
					snapshot->m_ForwardPlusCullGpuMilliseconds,
					snapshot->m_ForwardPlusOpaqueGpuMilliseconds, forwardPlusTotal);
				ImGui::TextColored(snapshot->m_LatestForwardPlusSampleLower
					? ImVec4(0.35f, 0.9f, 0.45f, 1.0f)
					: ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
					snapshot->m_LatestForwardPlusSampleLower
					? "Latest Forward+ sample is lower"
					: "Latest Forward+ sample is not lower");
				ImGui::TextDisabled(
					"Latest completed sample per mode; use a repeatable multi-frame capture to establish crossover.");
			}
			else
			{
				ImGui::TextDisabled(
					"Capture Legacy, then Forward+ with HDR diff disabled, to compare performance.");
			}
		}
	}
}
