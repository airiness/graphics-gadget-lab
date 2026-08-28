#include "DevTools/DevelopGui/Panels/PostProcessInspectorPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/PostProcessDiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHIFormat.h"

#include <imgui.h>

namespace gglab
{
	namespace
	{
		const char* GetTapName(PostProcessDebugTap tap) noexcept
		{
			switch (tap)
			{
			case PostProcessDebugTap::SceneColor:
				return "Scene Color";
			case PostProcessDebugTap::BloomPrefilter:
				return "Bloom Prefilter";
			case PostProcessDebugTap::BloomPyramid:
				return "Bloom Pyramid";
			case PostProcessDebugTap::BloomResult:
				return "Bloom Result";
			case PostProcessDebugTap::SceneDepthRaw:
				return "Scene Depth / Raw";
			case PostProcessDebugTap::SceneDepthLinearViewZ:
				return "Scene Depth / Linear View Z";
			case PostProcessDebugTap::GTAORawAO:
				return "GTAO / Raw AO";
			case PostProcessDebugTap::GTAOHalfDepthViewZ:
				return "GTAO / Half Depth View Z";
			case PostProcessDebugTap::GTAOReconstructedNormal:
				return "GTAO / Reconstructed Normal";
			case PostProcessDebugTap::GTAOSelectedSurfaceOffset:
				return "GTAO / Selected Surface Offset";
			case PostProcessDebugTap::GTAODenoiseX:
				return "GTAO / Denoise X";
			case PostProcessDebugTap::GTAODenoiseY:
				return "GTAO / Denoise Y";
			case PostProcessDebugTap::GTAOFinalAO:
				return "GTAO / Final AO";
			case PostProcessDebugTap::GTAOAOOnlyLightingContribution:
				return "GTAO / AO-only Lighting Contribution";
			case PostProcessDebugTap::TemporalMotionDirection:
				return "Temporal / Motion Direction";
			case PostProcessDebugTap::TemporalMotionMagnitude:
				return "Temporal / Motion Magnitude";
			case PostProcessDebugTap::TemporalHistoryColor:
				return "Temporal / Accumulated History Color";
			case PostProcessDebugTap::TemporalReprojectionUV:
				return "Temporal / Reprojection UV";
			case PostProcessDebugTap::TemporalRejection:
				return "Temporal / Rejection Reason";
			case PostProcessDebugTap::TemporalHistoryWeight:
				return "Temporal / History Weight";
			default:
				return "Unknown";
			}
		}

		bool DrawTapCombo(PostProcessDebugTap& tap) noexcept
		{
			bool changed = false;
			if (ImGui::BeginCombo("Tap", GetTapName(tap)))
			{
				constexpr PostProcessDebugTap Taps[] = {
					PostProcessDebugTap::SceneColor,
					PostProcessDebugTap::BloomPrefilter,
					PostProcessDebugTap::BloomPyramid,
					PostProcessDebugTap::BloomResult,
					PostProcessDebugTap::SceneDepthRaw,
					PostProcessDebugTap::SceneDepthLinearViewZ,
					PostProcessDebugTap::GTAORawAO,
					PostProcessDebugTap::GTAOHalfDepthViewZ,
					PostProcessDebugTap::GTAOReconstructedNormal,
					PostProcessDebugTap::GTAOSelectedSurfaceOffset,
					PostProcessDebugTap::GTAODenoiseX,
					PostProcessDebugTap::GTAODenoiseY,
					PostProcessDebugTap::GTAOFinalAO,
					PostProcessDebugTap::GTAOAOOnlyLightingContribution,
					PostProcessDebugTap::TemporalMotionDirection,
					PostProcessDebugTap::TemporalMotionMagnitude,
					PostProcessDebugTap::TemporalHistoryColor,
					PostProcessDebugTap::TemporalReprojectionUV,
					PostProcessDebugTap::TemporalRejection,
					PostProcessDebugTap::TemporalHistoryWeight,
				};
				for (const auto candidate : Taps)
				{
					const bool selected = candidate == tap;
					if (ImGui::Selectable(GetTapName(candidate), selected))
					{
						tap = candidate;
						changed = true;
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		const PostProcessTextureDiagnostics* ResolveSelectedTexture(
			const PostProcessDiagnosticsSnapshot& snapshot,
			PostProcessDebugSelection selection) noexcept
		{
			switch (selection.m_Tap)
			{
			case PostProcessDebugTap::SceneColor:
				return &snapshot.m_SceneColor;
			case PostProcessDebugTap::BloomPrefilter:
				return &snapshot.m_BloomPrefilter;
			case PostProcessDebugTap::BloomPyramid:
				return selection.m_BloomPyramidLevel < snapshot.m_BloomLevelCount
					? &snapshot.m_BloomPyramid[selection.m_BloomPyramidLevel]
					: nullptr;
			case PostProcessDebugTap::BloomResult:
				return &snapshot.m_BloomResult;
			default:
				return nullptr;
			}
		}

		void DrawTextureRow(
			const char* label, const PostProcessTextureDiagnostics& texture) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			if (texture.m_Available)
			{
				ImGui::Text("%u x %u", texture.m_Width, texture.m_Height);
			}
			else
			{
				ImGui::TextDisabled("Unavailable");
			}
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(
				texture.m_Available ? GetRHIFormatInfo(texture.m_Format).m_Name : "-");
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.1f KiB", static_cast<double>(texture.m_LogicalBytes) / 1024.0);
		}
	}

	void PostProcessInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto* renderer = context.m_Renderer;
		auto* diagnostics = context.m_Diagnostics;
		if (!renderer || !diagnostics)
		{
			ImGui::TextDisabled("Renderer diagnostics are unavailable.");
			return;
		}
		auto* registry = renderer->GetRenderResourceRegistry();
		if (!registry)
		{
			ImGui::TextDisabled("Render resource registry is unavailable.");
			return;
		}

		const auto* snapshot = diagnostics->GetSnapshot<PostProcessDiagnosticsSnapshot>();
		if (!snapshot)
		{
			ImGui::TextDisabled("Waiting for post-process diagnostics...");
			return;
		}

		PostProcessDebugSelection selection = registry->GetPostProcessPreviewSelection();
		bool selectionChanged = DrawTapCombo(selection.m_Tap);
		if (selection.m_Tap == PostProcessDebugTap::BloomPyramid)
		{
			const int maxLevel = snapshot->m_BloomLevelCount > 0
				? static_cast<int>(snapshot->m_BloomLevelCount - 1u)
				: 0;
			const uint32_t clampedLevel = static_cast<uint32_t>(
				std::clamp(static_cast<int>(selection.m_BloomPyramidLevel), 0, maxLevel));
			if (selection.m_BloomPyramidLevel != clampedLevel)
			{
				selection.m_BloomPyramidLevel = clampedLevel;
				selectionChanged = true;
			}
			int level = static_cast<int>(selection.m_BloomPyramidLevel);
			if (ImGui::SliderInt("Pyramid Level", &level, 0, maxLevel))
			{
				selection.m_BloomPyramidLevel = static_cast<uint32_t>(level);
				selectionChanged = true;
			}
		}
		if (selectionChanged)
		{
			registry->SetPostProcessPreviewSelection(selection);
		}

		float exposureEV = registry->GetPostProcessPreviewExposureEV();
		if (ImGui::SliderFloat("Preview Exposure", &exposureEV, -8.0f, 8.0f, "%+.2f EV"))
		{
			registry->SetPostProcessPreviewExposureEV(exposureEV);
		}
		if (ImGui::Button("Reset Preview Exposure"))
		{
			registry->SetPostProcessPreviewExposureEV(0.0f);
		}

		registry->RequestPostProcessPreview();
		const auto* selectedTexture = ResolveSelectedTexture(*snapshot, selection);
		const bool depthSelection = selection.m_Tap == PostProcessDebugTap::SceneDepthRaw ||
			selection.m_Tap == PostProcessDebugTap::SceneDepthLinearViewZ;
		const bool gtaoSelection = selection.m_Tap == PostProcessDebugTap::GTAORawAO ||
			selection.m_Tap == PostProcessDebugTap::GTAOHalfDepthViewZ ||
			selection.m_Tap == PostProcessDebugTap::GTAOReconstructedNormal ||
			selection.m_Tap == PostProcessDebugTap::GTAOSelectedSurfaceOffset ||
			selection.m_Tap == PostProcessDebugTap::GTAODenoiseX ||
			selection.m_Tap == PostProcessDebugTap::GTAODenoiseY ||
			selection.m_Tap == PostProcessDebugTap::GTAOFinalAO ||
			selection.m_Tap == PostProcessDebugTap::GTAOAOOnlyLightingContribution;
		const bool temporalMotionSelection =
			selection.m_Tap == PostProcessDebugTap::TemporalMotionDirection ||
			selection.m_Tap == PostProcessDebugTap::TemporalMotionMagnitude;
		const bool temporalAASelection =
			selection.m_Tap == PostProcessDebugTap::TemporalHistoryColor ||
			selection.m_Tap == PostProcessDebugTap::TemporalReprojectionUV ||
			selection.m_Tap == PostProcessDebugTap::TemporalRejection ||
			selection.m_Tap == PostProcessDebugTap::TemporalHistoryWeight;
		if (depthSelection && snapshot->m_SceneDepth.m_Available)
		{
			ImGui::TextDisabled("Source: %u x %u, %s resource, %s SRV",
				snapshot->m_SceneDepth.m_Width, snapshot->m_SceneDepth.m_Height,
				GetRHIFormatInfo(snapshot->m_SceneDepth.m_ResourceFormat).m_Name,
				GetRHIFormatInfo(snapshot->m_SceneDepth.m_SrvFormat).m_Name);
		}
		else if (gtaoSelection)
		{
			ImGui::TextDisabled("Source: transient GTAO evaluation/filter surface.");
		}
		else if (temporalMotionSelection)
		{
			ImGui::TextDisabled(
				"Source: active transient R16G16Float motion vectors in UV delta units.");
		}
		else if (temporalAASelection)
		{
			ImGui::TextDisabled("Source: active TAA history or reprojection diagnostics surface.");
		}
		else if (!selectedTexture || !selectedTexture->m_Available)
		{
			ImGui::TextDisabled(
				"The selected tap is unavailable in the current pipeline configuration.");
		}
		else
		{
			ImGui::TextDisabled("Source: %u x %u, %s, pre-exposure %.3f", selectedTexture->m_Width,
				selectedTexture->m_Height, GetRHIFormatInfo(selectedTexture->m_Format).m_Name,
				selectedTexture->m_PreExposure);
		}

		using TextureIndex = RenderResourceRegistry::TextureIndex;
		const auto* previewDesc = registry->GetTextureDesc(TextureIndex::Preview_PostProcess);
		if (registry->HasPublishedPostProcessPreview() && previewDesc)
		{
			const auto published = registry->GetPublishedPostProcessPreviewSelection();
			if (published != selection)
			{
				ImGui::TextDisabled("Preview update pending...");
			}
			const ImTextureID textureId =
				devtools::ResolveImGuiTextureId(context.m_DevelopGuiSystem,
					registry->GetSrvDescriptor(TextureIndex::Preview_PostProcess));
			if (textureId)
			{
				const float availableWidth = std::max(ImGui::GetContentRegionAvail().x, 64.0f);
				const float imageWidth = std::min(availableWidth, 768.0f);
				const float aspect = static_cast<float>(previewDesc->m_Extent.m_Height) /
					static_cast<float>(previewDesc->m_Extent.m_Width);
				ImGui::Image(textureId, ImVec2(imageWidth, imageWidth * aspect));
			}
		}
		else
		{
			ImGui::TextDisabled("Preview will be published on the next rendered frame.");
		}

		if (ImGui::CollapsingHeader("Scene Depth", ImGuiTreeNodeFlags_DefaultOpen))
		{
			const auto& depth = snapshot->m_SceneDepth;
			if (!depth.m_Available)
			{
				ImGui::TextDisabled("Scene depth is unavailable.");
			}
			else
			{
				ImGui::Text("Extent: %u x %u", depth.m_Width, depth.m_Height);
				ImGui::Text("Resource / DSV / SRV: %s / %s / %s",
					GetRHIFormatInfo(depth.m_ResourceFormat).m_Name,
					GetRHIFormatInfo(depth.m_DsvFormat).m_Name,
					GetRHIFormatInfo(depth.m_SrvFormat).m_Name);
				ImGui::Text("Clear: %.3f (%s) | Convention: %s", depth.m_ClearDepth,
					depth.m_HasTypedClear ? "typed" : "none",
					depth.m_Convention == DepthConvention::Reversed ? "Reversed-Z" : "Standard-Z");
				ImGui::TextDisabled(
					"Filter DisplayView.DepthBuffer in RenderGraph Inspector to inspect its access chain.");
			}
		}

		if (ImGui::CollapsingHeader("Bloom Resources", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Levels: %u | Logical footprint: %.1f KiB", snapshot->m_BloomLevelCount,
				static_cast<double>(snapshot->m_BloomLogicalBytes) / 1024.0);
			if (ImGui::BeginTable("PostProcessBloomResources", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Extent");
				ImGui::TableSetupColumn("Format");
				ImGui::TableSetupColumn("Logical Size");
				ImGui::TableHeadersRow();
				DrawTextureRow("Prefilter", snapshot->m_BloomPrefilter);
				for (uint32_t level = 1; level < snapshot->m_BloomLevelCount; ++level)
				{
					const std::string label = std::format("Pyramid {}", level);
					DrawTextureRow(label.c_str(), snapshot->m_BloomPyramid[level]);
				}
				DrawTextureRow("Result", snapshot->m_BloomResult);
				ImGui::EndTable();
			}
			ImGui::TextDisabled(
				"Logical footprint is format bytes x texels; allocator padding and aliasing are excluded.");
		}

		if (ImGui::CollapsingHeader("GPU Timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto* gpuProfiler = renderer->GetGpuProfiler();
			if (gpuProfiler)
			{
				bool enabled = gpuProfiler->IsEnabled();
				if (ImGui::Checkbox("GPU Profiling", &enabled))
				{
					gpuProfiler->SetEnabled(enabled);
				}
			}
			if (!snapshot->m_GpuProfilerEnabled)
			{
				ImGui::TextDisabled("Enable GPU profiling to collect per-pass timestamps.");
			}
			else if (!snapshot->m_GpuTimingAvailable)
			{
				ImGui::TextDisabled("Waiting for a completed GPU timestamp frame...");
			}
			else
			{
				ImGui::Text("Post Process: %.3f ms | Bloom: %.3f ms | Frame %llu",
					snapshot->m_PostProcessGpuMilliseconds, snapshot->m_BloomGpuMilliseconds,
					static_cast<unsigned long long>(snapshot->m_GpuFrameIndex));
				if (ImGui::BeginTable(
					"PostProcessGpuPasses", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
				{
					ImGui::TableSetupColumn("Pass");
					ImGui::TableSetupColumn("GPU ms");
					ImGui::TableSetupColumn("Calls");
					ImGui::TableHeadersRow();
					for (const auto& pass : snapshot->m_GpuPasses)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(pass.m_Name.c_str());
						ImGui::TableSetColumnIndex(1);
						ImGui::Text("%.3f", pass.m_Milliseconds);
						ImGui::TableSetColumnIndex(2);
						ImGui::Text("%u", pass.m_CallCount);
					}
					ImGui::EndTable();
				}
			}
		}
	}
}
