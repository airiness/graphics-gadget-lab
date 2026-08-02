#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/GTAOInspectorPanel.h"

#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "DevTools/DevToolsRuntime.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/GTAODiagnosticsSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Graphics/RHI/RHIFormat.h"
#include "Graphics/RHI/RHITextureValidation.h"

namespace gglab
{
	namespace
	{
		const char* GetStatusName(GTAOFrameStatus status) noexcept
		{
			switch (status)
			{
			case GTAOFrameStatus::Disabled:
				return "Disabled";
			case GTAOFrameStatus::Active:
				return "Active";
			case GTAOFrameStatus::CoreCapabilityUnavailable:
				return "Unavailable: core texture capability";
			case GTAOFrameStatus::PipelineUnavailable:
				return "Unavailable: pipeline preparation";
			case GTAOFrameStatus::RenderSceneUnavailable:
				return "Idle: render scene unavailable";
			case GTAOFrameStatus::DepthCoverageUnavailable:
				return "Idle: depth-prepass coverage unavailable";
			case GTAOFrameStatus::NoOpaqueDraws:
				return "Idle: no opaque draws";
			}
			return "Unknown";
		}

		const char* GetTapName(PostProcessDebugTap tap) noexcept
		{
			switch (tap)
			{
			case PostProcessDebugTap::GTAORawAO:
				return "Raw AO";
			case PostProcessDebugTap::GTAOHalfDepthViewZ:
				return "Selected Half Depth / View-Z";
			case PostProcessDebugTap::GTAOReconstructedNormal:
				return "Reconstructed Normal";
			case PostProcessDebugTap::GTAOSelectedSurfaceOffset:
				return "Selected 2x2 Surface Offset";
			case PostProcessDebugTap::GTAODenoiseX:
				return "Denoise X";
			case PostProcessDebugTap::GTAODenoiseY:
				return "Denoise Y";
			case PostProcessDebugTap::GTAOFinalAO:
				return "Final AO Visibility";
			case PostProcessDebugTap::GTAOAOOnlyLightingContribution:
				return "AO-only Lighting Contribution";
			default:
				return "GTAO Preview";
			}
		}

		bool DrawPreviewTapCombo(PostProcessDebugTap& tap) noexcept
		{
			constexpr PostProcessDebugTap Taps[] = {
				PostProcessDebugTap::GTAORawAO,
				PostProcessDebugTap::GTAOHalfDepthViewZ,
				PostProcessDebugTap::GTAOReconstructedNormal,
				PostProcessDebugTap::GTAOSelectedSurfaceOffset,
				PostProcessDebugTap::GTAODenoiseX,
				PostProcessDebugTap::GTAODenoiseY,
				PostProcessDebugTap::GTAOFinalAO,
				PostProcessDebugTap::GTAOAOOnlyLightingContribution,
			};
			bool changed = false;
			if (ImGui::BeginCombo("Preview Tap##GTAO", GetTapName(tap)))
			{
				for (const PostProcessDebugTap candidate : Taps)
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

		void DrawSettingsControls(GTAOSettings& settings) noexcept
		{
			ImGui::Checkbox("Enabled##GTAOOverride", &settings.m_Enabled);
			ImGui::DragFloat("Radius", &settings.m_Radius, 0.01f, 0.01f, 10.0f, "%.3f m");
			ImGui::DragFloat(
				"Falloff Start", &settings.m_FalloffStart, 0.01f, 0.0f, 10.0f, "%.3f m");
			ImGui::DragFloat(
				"Falloff End", &settings.m_FalloffEnd, 0.01f, 0.0f, 10.0f, "%.3f m");
			ImGui::DragFloat(
				"Thickness Bias", &settings.m_Thickness, 0.005f, 0.0f, 10.0f, "%.3f m");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip(
					"Rejects near self-occlusion in the current horizon approximation. Lower values "
					"produce stronger contact occlusion.");
			}
			ImGui::DragFloat("Power", &settings.m_Power, 0.02f, 0.1f, 8.0f, "%.2f");
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Applied after upsample. Values above 1 darken occluded visibility.");
			}
			int directionCount = static_cast<int>(settings.m_DirectionCount);
			if (ImGui::SliderInt("Directions", &directionCount, 1, GTAOMaxDirectionCount))
			{
				settings.m_DirectionCount = static_cast<uint32_t>(directionCount);
			}
			int stepCount = static_cast<int>(settings.m_StepCount);
			if (ImGui::SliderInt("Steps", &stepCount, 1, GTAOMaxStepCount))
			{
				settings.m_StepCount = static_cast<uint32_t>(stepCount);
			}
			int denoiseRadius = static_cast<int>(settings.m_DenoiseRadius);
			if (ImGui::SliderInt("Denoise Radius", &denoiseRadius, 1, GTAOMaxDenoiseRadius))
			{
				settings.m_DenoiseRadius = static_cast<uint32_t>(denoiseRadius);
			}
			const char* formats[] = { "Prefer R8 Unorm", "Force R16 Float" };
			int format = static_cast<int>(settings.m_FinalAOFormatPreference);
			if (ImGui::Combo("Final AO Format", &format, formats, std::size(formats)))
			{
				settings.m_FinalAOFormatPreference =
					static_cast<GTAOFinalAOFormatPreference>(format);
			}
		}

		void DrawTextureRow(const char* name, const GTAOTextureDiagnostics& texture) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(name);
			ImGui::TableSetColumnIndex(1);
			if (texture.m_Available)
			{
				ImGui::Text("%u x %u", texture.m_Width, texture.m_Height);
			}
			else
			{
				ImGui::TextDisabled("Not allocated");
			}
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(
				texture.m_Available ? GetRHIFormatInfo(texture.m_Format).m_Name : "-");
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%.1f KiB", static_cast<double>(texture.m_LogicalBytes) / 1024.0);
		}

		void DrawCapabilityRow(
			const char* name, const GTAOSurfaceFormatSupport& support) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(name);
			ImGui::TableSetColumnIndex(1);
			if (support.m_ShaderResource.IsSupported())
			{
				ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Supported");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.25f, 1.0f), "%s",
					RHITextureSupportReasonText(support.m_ShaderResource.m_Reason).data());
			}
			ImGui::TableSetColumnIndex(2);
			if (support.m_TypedUavStore.IsSupported())
			{
				ImGui::TextColored(ImVec4(0.35f, 0.9f, 0.45f, 1.0f), "Supported");
			}
			else
			{
				ImGui::TextColored(ImVec4(0.95f, 0.3f, 0.25f, 1.0f), "%s",
					RHITextureSupportReasonText(support.m_TypedUavStore.m_Reason).data());
			}
		}

		void DrawSettingsComparison(const GTAODiagnosticsSnapshot& snapshot) noexcept
		{
			if (!ImGui::BeginTable("GTAOSettingsLayers", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				return;
			}
			ImGui::TableSetupColumn("Setting");
			ImGui::TableSetupColumn("Authoring");
			ImGui::TableSetupColumn("Requested");
			ImGui::TableSetupColumn("Resolved");
			ImGui::TableHeadersRow();

			const auto drawText = [](const char* name, const char* authoring,
				const char* requested, const char* resolved) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(authoring);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(requested);
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(resolved);
				};
			const auto drawFloat = [](const char* name, float authoring, float requested,
				float resolved) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name);
					ImGui::TableNextColumn();
					ImGui::Text("%.3f", authoring);
					ImGui::TableNextColumn();
					ImGui::Text("%.3f", requested);
					ImGui::TableNextColumn();
					ImGui::Text("%.3f", resolved);
				};
			const auto drawUInt = [](const char* name, uint32_t authoring, uint32_t requested,
				uint32_t resolved) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(name);
					ImGui::TableNextColumn();
					ImGui::Text("%u", authoring);
					ImGui::TableNextColumn();
					ImGui::Text("%u", requested);
					ImGui::TableNextColumn();
					ImGui::Text("%u", resolved);
				};
			const GTAOSettings& authoring = snapshot.m_AuthoringSettings;
			const GTAOSettings& requested = snapshot.m_RequestedSettings;
			const GTAOSettings& resolved = snapshot.m_ResolvedSettings;
			drawText("Enabled", authoring.m_Enabled ? "Yes" : "No",
				requested.m_Enabled ? "Yes" : "No", resolved.m_Enabled ? "Yes" : "No");
			drawFloat("Radius", authoring.m_Radius, requested.m_Radius, resolved.m_Radius);
			drawFloat("Falloff Start", authoring.m_FalloffStart, requested.m_FalloffStart,
				resolved.m_FalloffStart);
			drawFloat("Falloff End", authoring.m_FalloffEnd, requested.m_FalloffEnd,
				resolved.m_FalloffEnd);
			drawFloat("Thickness Bias", authoring.m_Thickness, requested.m_Thickness,
				resolved.m_Thickness);
			drawFloat("Power", authoring.m_Power, requested.m_Power, resolved.m_Power);
			drawUInt("Directions", authoring.m_DirectionCount, requested.m_DirectionCount,
				resolved.m_DirectionCount);
			drawUInt("Steps", authoring.m_StepCount, requested.m_StepCount,
				resolved.m_StepCount);
			drawUInt("Denoise Radius", authoring.m_DenoiseRadius, requested.m_DenoiseRadius,
				resolved.m_DenoiseRadius);
			const auto formatName = [](GTAOFinalAOFormatPreference preference) noexcept
				{
					return preference == GTAOFinalAOFormatPreference::PreferR8Unorm
						? "Prefer R8"
						: "Force R16";
				};
			drawText("Format Request", formatName(authoring.m_FinalAOFormatPreference),
				formatName(requested.m_FinalAOFormatPreference),
				formatName(resolved.m_FinalAOFormatPreference));
			ImGui::EndTable();
		}
	}

	void GTAOInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer || !context.m_Diagnostics)
		{
			ImGui::TextDisabled("Renderer diagnostics are unavailable.");
			return;
		}
		const auto* snapshot = context.m_Diagnostics->GetSnapshot<GTAODiagnosticsSnapshot>();
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("GTAO diagnostics are unavailable for the current pipeline.");
			return;
		}

		ImGui::Text("Status: %s", GetStatusName(snapshot->m_Status));
		ImGui::Text("Settings source: %s",
			snapshot->m_OverrideActive ? "DevTools override" : "Active profile");
		if (snapshot->m_UsesFinalAOFormatFallback)
		{
			ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f),
				"FinalAO fallback: R8Unorm -> R16Float");
		}

		if (ImGui::CollapsingHeader("Controls", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto* overrides = context.m_ViewRenderSettingsOverrides;
			if (!overrides)
			{
				ImGui::TextDisabled("View-render settings controls are unavailable.");
			}
			else
			{
				bool overrideActive = overrides->m_GTAO.m_IsActive;
				if (ImGui::Checkbox("Override Active Profile##GTAO", &overrideActive))
				{
					if (overrideActive && !overrides->m_GTAO.m_IsActive)
					{
						overrides->m_GTAO.m_Settings = snapshot->m_AuthoringSettings;
					}
					overrides->m_GTAO.m_IsActive = overrideActive;
				}
				ImGui::TextDisabled("Controls apply to the active view on the next frame.");
				if (!overrides->m_GTAO.m_IsActive)
				{
					GTAOSettings readOnlySettings = snapshot->m_RequestedSettings;
					ImGui::BeginDisabled();
					DrawSettingsControls(readOnlySettings);
					ImGui::EndDisabled();
				}
				else
				{
					DrawSettingsControls(overrides->m_GTAO.m_Settings);
					if (ImGui::Button("Reset Override to Active Profile"))
					{
						overrides->m_GTAO.m_Settings = snapshot->m_AuthoringSettings;
					}
				}
			}
			ImGui::SeparatorText("Settings Layers");
			DrawSettingsComparison(*snapshot);
		}

		if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
		{
			auto* registry = context.m_Renderer->GetRenderResourceRegistry();
			GGLAB_ASSERT_NOT_NULL(registry);
			PostProcessDebugSelection selection = registry->GetPostProcessPreviewSelection();
			if (selection.m_Tap < PostProcessDebugTap::GTAORawAO ||
				selection.m_Tap > PostProcessDebugTap::GTAOAOOnlyLightingContribution)
			{
				selection.m_Tap = PostProcessDebugTap::GTAOFinalAO;
			}
			if (DrawPreviewTapCombo(selection.m_Tap))
			{
				registry->SetPostProcessPreviewSelection(selection);
			}
			registry->RequestPostProcessPreview();
			if (selection.m_Tap == PostProcessDebugTap::GTAOFinalAO)
			{
				ImGui::TextDisabled("FinalAO is visibility: white is unoccluded, black is occluded.");
			}
			else if (selection.m_Tap == PostProcessDebugTap::GTAOAOOnlyLightingContribution)
			{
				float exposureEV = registry->GetPostProcessPreviewExposureEV();
				if (ImGui::SliderFloat(
					"Contribution Exposure", &exposureEV, -8.0f, 8.0f, "%+.2f EV"))
				{
					registry->SetPostProcessPreviewExposureEV(exposureEV);
				}
			}

			using TextureIndex = RenderResourceRegistry::TextureIndex;
			const auto* previewDesc = registry->GetTextureDesc(TextureIndex::Preview_PostProcess);
			const bool published = registry->HasPublishedPostProcessPreview() && previewDesc &&
				registry->GetPublishedPostProcessPreviewSelection() == selection;
			if (!published)
			{
				ImGui::TextDisabled(snapshot->m_Status == GTAOFrameStatus::Active
					? "Preview update pending..."
					: "The selected preview requires an active GTAO frame.");
			}
			else
			{
				const ImTextureID textureId = devtools::ResolveImGuiTextureId(
					context.m_DevelopGuiSystem,
					registry->GetSrvDescriptor(TextureIndex::Preview_PostProcess));
				if (textureId)
				{
					const float width = std::min(
						std::max(ImGui::GetContentRegionAvail().x, 64.0f), 768.0f);
					const float aspect = static_cast<float>(previewDesc->m_Extent.m_Height) /
						static_cast<float>(previewDesc->m_Extent.m_Width);
					ImGui::Image(textureId, ImVec2(width, width * aspect));
				}
			}
		}

		if (ImGui::CollapsingHeader("Resources", ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Text("Logical footprint: %.2f MiB core + %.2f MiB diagnostics = %.2f MiB",
				static_cast<double>(snapshot->m_CoreLogicalBytes) / (1024.0 * 1024.0),
				static_cast<double>(snapshot->m_DiagnosticLogicalBytes) / (1024.0 * 1024.0),
				static_cast<double>(snapshot->m_TotalLogicalBytes) / (1024.0 * 1024.0));
			if (ImGui::BeginTable("GTAOResources", 4,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Extent");
				ImGui::TableSetupColumn("Format");
				ImGui::TableSetupColumn("Logical Size");
				ImGui::TableHeadersRow();
				DrawTextureRow("Raw AO", snapshot->m_RawAO);
				DrawTextureRow("Half Depth View-Z", snapshot->m_HalfDepthViewZ);
				DrawTextureRow("Denoise X", snapshot->m_DenoiseX);
				DrawTextureRow("Denoise Y", snapshot->m_DenoiseY);
				DrawTextureRow("Final AO", snapshot->m_FinalAO);
				DrawTextureRow("Reconstructed Normal", snapshot->m_ReconstructedNormal);
				DrawTextureRow("Selected Surface Offset", snapshot->m_SelectedSurfaceOffset);
				DrawTextureRow("AO-only Lighting", snapshot->m_AOOnlyLightingContribution);
				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("Capabilities"))
		{
			if (ImGui::BeginTable("GTAOCapabilities", 3,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
			{
				ImGui::TableSetupColumn("Format");
				ImGui::TableSetupColumn("SRV Load/Sample");
				ImGui::TableSetupColumn("Typed UAV View/Store");
				ImGui::TableHeadersRow();
				DrawCapabilityRow("R8Unorm", snapshot->m_Capabilities.m_FinalAO.m_PreferredR8Unorm);
				DrawCapabilityRow("R16Float", snapshot->m_Capabilities.m_R16Float);
				DrawCapabilityRow("R32Float", snapshot->m_Capabilities.m_R32Float);
				DrawCapabilityRow("R16G16Float", snapshot->m_Capabilities.m_R16G16Float);
				DrawCapabilityRow(
					"R16G16B16A16Float", snapshot->m_Capabilities.m_R16G16B16A16Float);
				ImGui::EndTable();
			}
		}

		if (ImGui::CollapsingHeader("GPU Timing", ImGuiTreeNodeFlags_DefaultOpen))
		{
			GpuProfiler* profiler = context.m_Renderer->GetGpuProfiler();
			if (profiler)
			{
				bool enabled = profiler->IsEnabled();
				if (ImGui::Checkbox("GPU Profiling##GTAO", &enabled))
				{
					profiler->SetEnabled(enabled);
				}
			}
			if (!snapshot->m_GpuTimingAvailable)
			{
				ImGui::TextDisabled("Waiting for a completed GPU timestamp frame...");
			}
			else
			{
				ImGui::Text("Evaluate %.3f ms | Denoise X/Y %.3f / %.3f ms | Upsample %.3f ms",
					snapshot->m_EvaluateGpuMilliseconds, snapshot->m_DenoiseXGpuMilliseconds,
					snapshot->m_DenoiseYGpuMilliseconds, snapshot->m_UpsampleGpuMilliseconds);
				ImGui::Text("Total %.3f ms | Frame %llu", snapshot->m_TotalGpuMilliseconds,
					static_cast<unsigned long long>(snapshot->m_GpuFrameIndex));
				ImGui::TextDisabled(
					"The 1440p default target is approximately 1.5 ms; it is not a cross-GPU correctness gate.");
			}
		}
	}
}
