#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Scene/DirectionalLightTooling.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct ShadowInspectorPanelState
		{
			int m_SelectedCascade = 0;
			float m_PreviewSize = 384.0f;
			bool m_FlipPreviewY = false;
			bool m_ShowMatrices = false;
		};

		static bool DrawDirectionalShadowSettings(DirectionalShadowSettings& settings) noexcept
		{
			bool changed = false;
			changed |= ImGui::Checkbox("Enable Shadow Shading", &settings.m_Enable);

			ImGui::SeparatorText("Cascades");
			struct CascadePreset
			{
				const char* m_Label;
				uint32_t m_Count;
				uint32_t m_MapSize;
			};
			static constexpr std::array presets = {
				CascadePreset{ "1 Cascade / 2048", 1, 2048 },
				CascadePreset{ "4 Cascades / 1024", 4, 1024 },
				CascadePreset{ "4 Cascades / 2048", 4, 2048 },
			};
			const auto currentPreset = std::find_if(presets.begin(), presets.end(),
				[&](const CascadePreset& preset)
				{
					return settings.m_CascadeCount == preset.m_Count &&
						settings.m_ShadowMapSize == preset.m_MapSize;
				});
			if (ImGui::BeginCombo("Cascade Preset",
				currentPreset != presets.end() ? currentPreset->m_Label : "Custom"))
			{
				for (const auto& preset : presets)
				{
					if (ImGui::Selectable(preset.m_Label,
						currentPreset != presets.end() && &*currentPreset == &preset))
					{
						if (settings.m_CascadeCount != preset.m_Count || settings.m_ShadowMapSize != preset.m_MapSize)
						{
							settings.m_CascadeCount = preset.m_Count;
							settings.m_ShadowMapSize = preset.m_MapSize;
							changed = true;
						}
					}
				}
				ImGui::EndCombo();
			}
			int cascadeCount = static_cast<int>(settings.m_CascadeCount);
			if (ImGui::SliderInt("Cascade Count", &cascadeCount, 1, MaxDirectionalShadowCascades))
			{
				settings.m_CascadeCount = static_cast<uint32_t>(cascadeCount);
				changed = true;
			}
			int shadowMapSize = static_cast<int>(settings.m_ShadowMapSize);
			if (ImGui::SliderInt("Shadow Map Size", &shadowMapSize, 256, 8192))
			{
				settings.m_ShadowMapSize = static_cast<uint32_t>(shadowMapSize);
				changed = true;
			}
			changed |= ImGui::SliderFloat("Split Lambda", &settings.m_SplitLambda, 0.0f, 1.0f, "%.2f");
			changed |= ImGui::SliderFloat("Cascade Blend Fraction", &settings.m_CascadeBlendFraction, 0.0f, 0.5f, "%.2f");
			changed |= ImGui::SliderFloat("Distance Fade Fraction", &settings.m_DistanceFadeFraction, 0.0f, 0.5f, "%.2f");

			ImGui::SeparatorText("Projection");
			int fitMode = static_cast<int>(settings.m_FitMode);
			if (ImGui::Combo("Projection Fit", &fitMode, "Tight fit\0Stable sphere\0"))
			{
				settings.m_FitMode = static_cast<DirectionalShadowFitMode>(fitMode);
				changed = true;
			}
			ImGui::BeginDisabled(settings.m_FitMode != DirectionalShadowFitMode::StableSphere);
			changed |= ImGui::Checkbox("Texel Snapping", &settings.m_EnableTexelSnapping);
			ImGui::EndDisabled();
			changed |= ImGui::DragFloat(
				"Max Shadow Distance", &settings.m_MaxShadowDistance, 1.0f, 1.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat("Caster Extrusion Distance", &settings.m_CasterExtrusionDistance, 1.0f,
				0.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat(
				"Ortho Padding", &settings.m_OrthoPadding, 0.1f, 0.0f, 1000.0f, "%.2f");
			changed |= ImGui::DragFloat(
				"Depth Padding", &settings.m_DepthPadding, 0.5f, 0.0f, 10000.0f, "%.1f");

			ImGui::SeparatorText("Sampling / Bias");
			changed |= ImGui::Checkbox("3x3 PCF", &settings.m_EnablePCF);
			changed |= ImGui::DragFloat("Receiver Bias (texels)", &settings.m_ReceiverBiasTexels,
				0.05f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::DragFloat("Residual Slope Bias (texels)", &settings.m_ReceiverSlopeBiasTexels,
				0.05f, 0.0f, 8.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			changed |= ImGui::DragFloat("Max Receiver Slope", &settings.m_ReceiverMaxSlope,
				0.1f, 0.0f, 16.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
			ImGui::TextUnformatted("Automatic receiver-plane and bilinear footprint correction");
			return changed;
		}

		static void DrawShadowOverlays(ShadowVisualizationSettings& settings) noexcept
		{
			ImGui::Checkbox("Cascade Overlay", &settings.m_ShowCascadeOverlay);
			ImGui::SameLine();
			ImGui::Checkbox("Transition Overlay", &settings.m_ShowTransitionOverlay);
		}

		static void DrawShadowCapability(DevelopGuiContext& context) noexcept
		{
			ImGui::SeparatorText("Directional Shadow");
			auto light = context.m_DirectionalLight ? context.m_DirectionalLight->GetLight() : std::nullopt;
			if (!light)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Directional light is not found.");
				return;
			}

			auto* control = context.m_DirectionalLightControl;
			ImGui::BeginDisabled(!control);
			bool castShadows = light->m_ShadowSettings.has_value();
			bool changed = ImGui::Checkbox("Cast Shadows", &castShadows);
			if (changed)
			{
				if (castShadows)
				{
					light->m_ShadowSettings.emplace();
				}
				else
				{
					light->m_ShadowSettings.reset();
				}
			}
			if (light->m_ShadowSettings)
			{
				changed |= DrawDirectionalShadowSettings(*light->m_ShadowSettings);
			}
			else
			{
				ImGui::TextUnformatted("Directional light has no shadow settings.");
			}
			if (changed && control)
			{
				control->SetShadowSettings(light->m_Id, light->m_ShadowSettings);
			}
			ImGui::EndDisabled();
		}

		static void DrawShadowCamera(
			DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("Cascade Diagnostics");

			const auto* snapshot = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<ShadowDiagnosticsSnapshot>() : nullptr;
			if (!snapshot || snapshot->m_Cascades.empty())
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"Directional shadow render view is not available.");
				return;
			}

			const int cascadeCount = static_cast<int>(snapshot->m_Cascades.size());
			state.m_SelectedCascade = std::clamp(state.m_SelectedCascade, 0, cascadeCount - 1);
			ImGui::Text("Cascade count: %d", cascadeCount);
			if (cascadeCount > 1)
			{
				ImGui::SliderInt("Inspected Cascade / Preview Layer", &state.m_SelectedCascade, 0, cascadeCount - 1);
			}
			if (context.m_ShadowVisualizationSettings)
			{
				context.m_ShadowVisualizationSettings->m_PreviewCascade =
					static_cast<uint32_t>(state.m_SelectedCascade);
			}
			const auto& cascade = snapshot->m_Cascades[state.m_SelectedCascade];
			ImGui::Text("Main-camera split: %.3f - %.3f m", cascade.m_SplitNear, cascade.m_SplitFar);
			ImGui::Text("Transition begins: %.3f m", cascade.m_BlendStart);
			const RenderView* shadowView = &cascade.m_View;
			ImGui::Text("GPU view offset: %u", cascade.m_ViewIndex);
			const auto& projection = cascade.m_Projection;
			ImGui::Text("Fit: %s | Snapping: %s",
				projection.m_FitMode == DirectionalShadowFitMode::StableSphere ? "Stable sphere" : "Tight",
				projection.m_TexelSnappingApplied ? "On" : "Off");
			if (projection.m_FitMode == DirectionalShadowFitMode::StableSphere)
			{
				ImGui::Text("Sphere radius: %.4f m", projection.m_SphereRadius);
			}
			ImGui::Text("Extent XY: %.4f / %.4f m", projection.m_Extent.m_X, projection.m_Extent.m_Y);
			ImGui::Text("World units / texel: %.6f / %.6f", projection.m_WorldUnitsPerTexel.m_X,
				projection.m_WorldUnitsPerTexel.m_Y);
			ImGui::Text("Unsnapped center LS: %.5f / %.5f", projection.m_UnsnappedCenterLS.m_X,
				projection.m_UnsnappedCenterLS.m_Y);
			ImGui::Text("Resolved center LS: %.5f / %.5f", projection.m_CenterLS.m_X, projection.m_CenterLS.m_Y);
			if (projection.m_WorldUnitsPerTexel.m_X > 0.0f && projection.m_WorldUnitsPerTexel.m_Y > 0.0f)
			{
				ImGui::Text("Snap offset (texels): %.3f / %.3f",
					(projection.m_CenterLS.m_X - projection.m_UnsnappedCenterLS.m_X) / projection.m_WorldUnitsPerTexel.m_X,
					(projection.m_CenterLS.m_Y - projection.m_UnsnappedCenterLS.m_Y) / projection.m_WorldUnitsPerTexel.m_Y);
			}
			const auto& bias = cascade.m_Bias;
			ImGui::SeparatorText("Resolved Bias");
			ImGui::Text("Texel footprint: %.6f m | Depth span: %.3f m",
				bias.m_WorldUnitsPerTexel, bias.m_DepthSpan);
			ImGui::Text("Receiver constant: %.6f m / %.8f depth",
				bias.m_ReceiverConstantWorld, bias.m_ReceiverDepthBias);
			ImGui::Text("Receiver slope: %.6f m / %.8f depth",
				bias.m_ReceiverSlopeWorld, bias.m_ReceiverSlopeDepthBias);
			ImGui::Text("Max slope: %.2f | Max depth delta: %.8f", bias.m_ReceiverMaxSlope,
				bias.m_ReceiverDepthBias + bias.m_ReceiverSlopeDepthBias * bias.m_ReceiverMaxSlope);
			ImGui::Text("Shadow draws: %u | Culled instances: %u", cascade.m_ShadowDrawCount,
				cascade.m_QueueStatistics.m_CulledInstanceCount);
			ImGui::Text("Queue items: %u | Visible / total instances: %u / %u",
				cascade.m_QueueStatistics.m_DrawItemCount,
				cascade.m_QueueStatistics.m_VisibleInstanceCount,
				cascade.m_QueueStatistics.m_TotalInstanceCount);
			ImGui::TextUnformatted("Caster culling: disabled (conservative submission)");

			ImGui::Text("Position: %.3f, %.3f, %.3f", shadowView->m_CameraPosition.m_X,
				shadowView->m_CameraPosition.m_Y, shadowView->m_CameraPosition.m_Z);
			ImGui::Text("Near/Far: %.3f / %.3f", shadowView->m_Near, shadowView->m_Far);
			ImGui::Text("Viewport: %u x %u", shadowView->m_Width, shadowView->m_Height);
			ImGui::Checkbox("Show Matrices", &state.m_ShowMatrices);

			if (state.m_ShowMatrices)
			{
				devtools::DrawMatrix4x4Tree("View", shadowView->m_View);
				devtools::DrawMatrix4x4Tree("UnjitteredProjection", shadowView->m_UnjitteredProj);
				devtools::DrawMatrix4x4Tree(
					"UnjitteredViewProjection", shadowView->m_UnjitteredViewProj);
			}
		}

		static void DrawShadowPreview(
			DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("Shadow Map Preview");
			if (auto* settings = context.m_ShadowVisualizationSettings)
			{
				ImGui::Checkbox("2x2 Cascade Overview", &settings->m_PreviewAllCascades);
				ImGui::DragFloat("Preview Min Depth", &settings->m_PreviewMinDepth,
					0.001f, 0.0f, 1.0f, "%.4f");
				ImGui::DragFloat("Preview Max Depth", &settings->m_PreviewMaxDepth,
					0.001f, 0.0f, 1.0f, "%.4f");
				settings->m_PreviewMinDepth = std::clamp(settings->m_PreviewMinDepth, 0.0f, 1.0f);
				settings->m_PreviewMaxDepth = std::clamp(settings->m_PreviewMaxDepth, 0.0f, 1.0f);
				if (settings->m_PreviewMaxDepth <= settings->m_PreviewMinDepth)
				{
					settings->m_PreviewMaxDepth = std::min(settings->m_PreviewMinDepth + 0.001f, 1.0f);
				}
				ImGui::Checkbox("Invert Preview", &settings->m_PreviewInvert);
			}
			else
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"Shadow visualization settings are not available.");
			}
			ImGui::SliderFloat("Preview Size", &state.m_PreviewSize, 128.0f, 768.0f, "%.0f");
			ImGui::Checkbox("Flip Preview Y", &state.m_FlipPreviewY);
			if (context.m_ShadowPreviewControl)
			{
				context.m_ShadowPreviewControl->RequestShadowPreview();
			}

			const auto* snapshot = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<ShadowDiagnosticsSnapshot>()
				: nullptr;
			if (!snapshot)
			{
				ImGui::TextDisabled("Shadow diagnostics snapshot provider is not available.");
				return;
			}

			if (!snapshot->m_Available || !snapshot->m_DirectionalShadowMap.m_Available)
			{
				ImGui::TextColored(
					devtools::style::ErrorTextColor, "ShadowMap resource is not available.");
				return;
			}

			if (!snapshot->m_DirectionalShadowMapPreviewSource.m_Available)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"ShadowMap preview resource is not available.");
				return;
			}

			if (!context.m_ShadowPreview)
			{
				ImGui::TextDisabled("Shadow preview query is not available.");
				return;
			}

			const auto preview = context.m_ShadowPreview->GetShadowPreviewDiagnostics();
			if (!preview.m_Allocated)
			{
				ImGui::TextColored(
					devtools::style::ErrorTextColor, "ShadowMap preview texture is not allocated.");
				return;
			}

			const ImTextureID previewTextureId =
				devtools::ResolveImGuiTextureId(context.m_DevelopGuiSystem,
					preview.m_SrvDescriptor);

			const uint32_t cascadeCount = static_cast<uint32_t>(
				std::min(snapshot->m_Cascades.size(),
					static_cast<size_t>(snapshot->m_DirectionalShadowMap.m_ArraySize)));
			const bool overview = context.m_ShadowVisualizationSettings &&
				context.m_ShadowVisualizationSettings->m_PreviewAllCascades && cascadeCount > 1;
			ImGui::Text("Preview layout: %s", overview ? "2x2 cascade overview" : "selected layer");
			if (!overview) ImGui::Text("Selected layer: %d", state.m_SelectedCascade);

			if (!previewTextureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"ShadowMap preview SRV GPU handle is invalid.");
				return;
			}
			if (ImGui::TreeNode("Resource Details"))
			{
				ImGui::TextUnformatted("ShadowMap is a transient RenderGraph texture.");
				ImGui::Text("RG Size: %u | Array layers: %u", snapshot->m_ShadowMapSize,
					snapshot->m_DirectionalShadowMap.m_ArraySize);
				ImGui::Text("Texture Size: %llu x %u",
					static_cast<unsigned long long>(snapshot->m_DirectionalShadowMap.m_Extent.m_Width),
					snapshot->m_DirectionalShadowMap.m_Extent.m_Height);
				ImGui::Text("Format: %s",
					GetRHIFormatInfo(snapshot->m_DirectionalShadowMap.m_Format).m_Name);
				ImGui::Text("Preview RG Size: %u", snapshot->m_ShadowMapPreviewSize);
				ImGui::Text("Preview Texture Size: %u x %u", preview.m_Width, preview.m_Height);
				ImGui::Text("Preview Format: %s", GetRHIFormatInfo(preview.m_Format).m_Name);
				ImGui::Text("Preview SRV Format: %s", GetRHIFormatInfo(RHIFormat::R32Float).m_Name);
				ImGui::Text("Preview Shader Visible SRV Index: %u", preview.m_SrvDescriptor.m_Index);
				ImGui::TreePop();
			}

			const float previewSize = std::clamp(state.m_PreviewSize, 16.0f, 2048.0f);
			if (overview)
			{
				ImGui::TextDisabled("Shared display range; grayscale is per-cascade clip depth, not world distance.");
				ImGui::TextDisabled("Click a tile for full-size detail.");
				if (ImGui::BeginTable("CascadePreviewTiles", 2, ImGuiTableFlags_SizingFixedFit))
				{
					const float tileSize = previewSize * 0.5f;
					for (uint32_t index = 0; index < cascadeCount; ++index)
					{
						ImGui::TableNextColumn();
						ImGui::PushID(static_cast<int>(index));
						const auto& cascade = snapshot->m_Cascades[index];
						ImGui::Text("Cascade %u | split %.2f-%.2f m", index,
							cascade.m_SplitNear, cascade.m_SplitFar);
						ImGui::Text("Texel: %.5f / %.5f m", cascade.m_Projection.m_WorldUnitsPerTexel.m_X,
							cascade.m_Projection.m_WorldUnitsPerTexel.m_Y);
						const float u = static_cast<float>(index % 2) * 0.5f;
						const float v = static_cast<float>(index / 2) * 0.5f;
						const ImVec2 uv0(u, state.m_FlipPreviewY ? v + 0.5f : v);
						const ImVec2 uv1(u + 0.5f, state.m_FlipPreviewY ? v : v + 0.5f);
						if (ImGui::ImageButton("##CascadePreview", previewTextureId,
							ImVec2(tileSize, tileSize), uv0, uv1))
						{
							state.m_SelectedCascade = static_cast<int>(index);
							context.m_ShadowVisualizationSettings->m_PreviewCascade = index;
							context.m_ShadowVisualizationSettings->m_PreviewAllCascades = false;
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}
			else
			{
				const ImVec2 uv0 = state.m_FlipPreviewY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
				const ImVec2 uv1 = state.m_FlipPreviewY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
				ImGui::Image(previewTextureId, ImVec2(previewSize, previewSize), uv0, uv1);
			}
		}
	}

	void ShadowInspectorPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ShadowInspectorPanelState>();

		const auto light = context.m_DirectionalLight
			? context.m_DirectionalLight->GetLight() : std::nullopt;
		ImGui::Text("Status: %s", light && light->m_ShadowSettings &&
			light->m_ShadowSettings->m_Enable ? "Enabled" : "Disabled");
		DrawShadowCapability(context);
		DrawShadowPreview(context, state);
		if (ImGui::CollapsingHeader("Advanced: Cascade Diagnostics"))
		{
			DrawShadowCamera(context, state);
		}
		if (context.m_ShadowVisualizationSettings &&
			ImGui::CollapsingHeader("Scene Overlays"))
		{
			DrawShadowOverlays(*context.m_ShadowVisualizationSettings);
		}
	}
}
