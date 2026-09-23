#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Scene/DirectionalLightTooling.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/ShadowTemporalDiagnostics.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/CameraTooling.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
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
			ShadowTemporalDiagnostics m_Temporal;
			CameraEditSettings m_ReplayBase{};
			Vector3 m_ReplayRight = Vector3::UnitX;
			Vector3 m_ReplayForward = Vector3::Forward;
			uint64_t m_ReplayCameraId = 0;
			uint32_t m_ReplayStep = 0;
			bool m_ReplayActive = false;
		};

		static void DrawLightControl(DevelopGuiContext& context) noexcept
		{
			ImGui::SeparatorText("Light Control");
			auto light = context.m_DirectionalLight ? context.m_DirectionalLight->GetLight() : std::nullopt;
			if (!light)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor, "Directional light is not found.");
				return;
			}

			auto* control = context.m_DirectionalLightControl;
			ImGui::BeginDisabled(!control);
			float direction[3] = { light->m_Direction.m_X, light->m_Direction.m_Y, light->m_Direction.m_Z };
			if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f") && control)
			{
				control->SetDirection(light->m_Id, Vector3(direction[0], direction[1], direction[2]));
			}
			float color[3] = { light->m_Color.m_R, light->m_Color.m_G, light->m_Color.m_B };
			bool changed = ImGui::ColorEdit3("Color", color);
			changed |= ImGui::DragFloat("Intensity", &light->m_Intensity, 0.01f, 0.0f, 100.0f, "%.3f");
			if (changed && control)
			{
				light->m_Color.m_R = color[0];
				light->m_Color.m_G = color[1];
				light->m_Color.m_B = color[2];
				control->SetRadiance(light->m_Id, light->m_Color, light->m_Intensity);
			}
			ImGui::EndDisabled();
		}

		static bool DrawDirectionalShadowSettings(DirectionalShadowSettings& settings) noexcept
		{
			bool changed = false;
			ImGui::SeparatorText("General");
			changed |= ImGui::Checkbox("Enable", &settings.m_Enable);
			ImGui::SameLine();
			changed |= ImGui::Checkbox("3x3 PCF", &settings.m_EnablePCF);

			if (ImGui::Button("1 Cascade / 2048"))
			{
				settings.m_CascadeCount = 1;
				settings.m_ShadowMapSize = 2048;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("4 Cascades / 1024"))
			{
				settings.m_CascadeCount = 4;
				settings.m_ShadowMapSize = 1024;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("4 Cascades / 2048"))
			{
				settings.m_CascadeCount = 4;
				settings.m_ShadowMapSize = 2048;
				changed = true;
			}
			changed |= ImGui::SliderFloat("Split Lambda", &settings.m_SplitLambda, 0.0f, 1.0f, "%.2f");
			int fitMode = static_cast<int>(settings.m_FitMode);
			if (ImGui::Combo("Projection Fit", &fitMode, "Tight fit\0Stable sphere\0"))
			{
				settings.m_FitMode = static_cast<DirectionalShadowFitMode>(fitMode);
				changed = true;
			}
			ImGui::BeginDisabled(settings.m_FitMode != DirectionalShadowFitMode::StableSphere);
			changed |= ImGui::Checkbox("Texel Snapping", &settings.m_EnableTexelSnapping);
			ImGui::EndDisabled();
			ImGui::TextUnformatted("Main-camera splits | Overlapping cascade transitions");

			int shadowMapSize = static_cast<int>(settings.m_ShadowMapSize);
			if (ImGui::SliderInt("Shadow Map Size", &shadowMapSize, 256, 8192))
			{
				changed = true;
				settings.m_ShadowMapSize = static_cast<uint32_t>(std::max(shadowMapSize, 1));
			}

			ImGui::SeparatorText("Projection");
			changed |= ImGui::DragFloat(
				"Max Shadow Distance", &settings.m_MaxShadowDistance, 1.0f, 1.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat("Caster Extrusion Distance", &settings.m_CasterExtrusionDistance, 1.0f,
				0.0f, 10000.0f, "%.1f");
			changed |= ImGui::DragFloat(
				"Ortho Padding", &settings.m_OrthoPadding, 0.1f, 0.0f, 1000.0f, "%.2f");
			changed |= ImGui::DragFloat(
				"Depth Padding", &settings.m_DepthPadding, 0.5f, 0.0f, 10000.0f, "%.1f");

			ImGui::SeparatorText("Bias / Filtering");
			changed |= ImGui::SliderFloat("Cascade Blend Fraction", &settings.m_CascadeBlendFraction, 0.0f, 0.5f, "%.2f");
			changed |= ImGui::SliderFloat("Distance Fade Fraction", &settings.m_DistanceFadeFraction, 0.0f, 0.5f, "%.2f");
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
			ImGui::SeparatorText("Scene Overlays");
			ImGui::Checkbox("Cascade Overlay", &settings.m_ShowCascadeOverlay);
			ImGui::SameLine();
			ImGui::Checkbox("Transition Overlay", &settings.m_ShowTransitionOverlay);
			ImGui::TextDisabled("Blue / green / orange / purple: cascades; yellow: blend; pink: distance fade.");
		}

		static void DrawShadowCapability(DevelopGuiContext& context) noexcept
		{
			ImGui::SeparatorText("Shadow");
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
			ImGui::SeparatorText("Shadow Camera / Frustum");

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
				ImGui::SliderInt("Cascade / Preview Layer", &state.m_SelectedCascade, 0, cascadeCount - 1);
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

		static void DrawTemporalDiagnostics(
			DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("Temporal Diagnostics / CAM_ShadowStairs");
			const auto* shadow = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<ShadowDiagnosticsSnapshot>() : nullptr;
			const CameraToolingSnapshot cameras = context.m_Cameras
				? context.m_Cameras->GetCameras() : CameraToolingSnapshot{};
			const CameraToolingObservation* mainCamera = nullptr;
			for (const auto& camera : cameras.m_Cameras)
			{
				if (camera.m_RenderViewId == RenderViewID::Main) mainCamera = &camera;
			}
			const CameraReferenceView* stairs = nullptr;
			for (const auto& reference : cameras.m_ReferenceViews)
			{
				if (reference.m_Id == "CAM_ShadowStairs") stairs = &reference;
			}
			if (shadow && mainCamera)
			{
				(void) state.m_Temporal.Record(*shadow, mainCamera->m_Id,
					cameras.m_LastRestoredReferenceId);
			}
			else
			{
				state.m_Temporal.Reset();
				state.m_ReplayActive = false;
			}

			bool replayStarted = false;
			ImGui::BeginDisabled(!stairs || !mainCamera || !context.m_CameraControl);
			if (ImGui::Button("Replay stairs path") && stairs && mainCamera && context.m_CameraControl &&
				context.m_CameraControl->RestoreReferenceView(mainCamera->m_Id, stairs->m_Id))
			{
				const CameraToolingSnapshot restored = context.m_Cameras->GetCameras();
				const auto* camera = restored.FindCamera(mainCamera->m_Id);
				if (camera)
				{
					state.m_ReplayBase = camera->m_Settings;
					state.m_ReplayForward = (stairs->m_Target - stairs->m_Position).Normalized();
					state.m_ReplayRight = Vector3::UnitY.Cross(state.m_ReplayForward).Normalized();
					state.m_ReplayCameraId = camera->m_Id;
					state.m_ReplayStep = 0;
					state.m_ReplayActive = true;
					replayStarted = true;
					state.m_Temporal.Reset();
					if (context.m_GpuProfilingControl) context.m_GpuProfilingControl->RequestEnabled(true);
				}
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Stop replay")) state.m_ReplayActive = false;
			ImGui::SameLine();
			if (ImGui::Button("Clear comparison")) state.m_Temporal.Reset();
			ImGui::TextDisabled("121 fixed camera samples; replay starts from the authored reference pose.");
			if (state.m_ReplayActive)
			{
				if (!mainCamera || mainCamera->m_Id != state.m_ReplayCameraId ||
					(!replayStarted && cameras.m_LastRestoredReferenceId != "CAM_ShadowStairs") ||
					!context.m_CameraControl)
				{
					state.m_ReplayActive = false;
				}
				else
				{
					constexpr uint32_t ReplayLastStep = 120;
					const float phase = static_cast<float>(state.m_ReplayStep) /
						static_cast<float>(ReplayLastStep) * 6.28318530718f;
					CameraEditSettings sample = state.m_ReplayBase;
					sample.m_Position += state.m_ReplayRight * (0.8f * std::sin(phase)) +
						state.m_ReplayForward * (0.35f * (1.0f - std::cos(phase)));
					state.m_ReplayActive = context.m_CameraControl->SetCamera(mainCamera->m_Id, sample);
					(void) context.m_CameraControl->ResetVelocity(mainCamera->m_Id);
					if (state.m_ReplayStep++ == ReplayLastStep) state.m_ReplayActive = false;
				}
			}
			ImGui::Text("Replay sample: %u / 120", std::min(state.m_ReplayStep, 120u));

			const auto& samples = state.m_Temporal.GetSamples();
			ImGui::Text("Comparison frames: %u | automatic resets: %u",
				static_cast<uint32_t>(samples.size()), state.m_Temporal.GetResetCount());
			if (!samples.empty() && shadow && !shadow->m_Cascades.empty())
			{
				const auto& latest = samples.back();
				for (uint32_t index = 0; index < latest.m_CascadeCount; ++index)
				{
					const auto& delta = latest.m_ProjectionDeltaTexels[index];
					const auto& scale = latest.m_WorldUnitsPerTexel[index];
					const auto& scaleDelta = latest.m_TexelScaleDelta[index];
					const auto& gridError = latest.m_GridErrorTexels[index];
					ImGui::Text("Cascade %u texel scale: %.7f / %.7f m/texel",
						index, scale.m_X, scale.m_Y);
					if (latest.m_HasComparison)
					{
						const float previousX = scale.m_X - scaleDelta.m_X;
						const float previousY = scale.m_Y - scaleDelta.m_Y;
						const float percentX = previousX > 0.0f ? 100.0f * scaleDelta.m_X / previousX : 0.0f;
						const float percentY = previousY > 0.0f ? 100.0f * scaleDelta.m_Y / previousY : 0.0f;
						ImGui::Text("  Center delta: %+7.3f / %+7.3f current texels", delta.m_X, delta.m_Y);
						ImGui::Text("  Scale change: %+.7f / %+.7f m/texel (%+.3f%% / %+.3f%%)",
							scaleDelta.m_X, scaleDelta.m_Y, percentX, percentY);
					}
					else ImGui::TextDisabled("  Baseline frame: no adjacent-frame delta");
					ImGui::Text("  Fractional grid error: %+7.3f / %+7.3f texels",
						gridError.m_X, gridError.m_Y);
				}
				ImGui::TextDisabled("Resolved center versus nearest integer texel; zero is grid aligned.");
				const uint32_t selected = std::min(static_cast<uint32_t>(state.m_SelectedCascade),
					latest.m_CascadeCount - 1);
				std::array<float, ShadowTemporalDiagnostics::MaxSamples> magnitudes{};
				std::array<float, ShadowTemporalDiagnostics::MaxSamples> scaleChanges{};
				std::array<float, ShadowTemporalDiagnostics::MaxSamples> gridErrors{};
				float maxMagnitude = 1.0f;
				float maxScaleChange = 0.01f;
				for (size_t index = 0; index < samples.size(); ++index)
				{
					const auto& recorded = samples[index];
					const auto& delta = recorded.m_ProjectionDeltaTexels[selected];
					magnitudes[index] = std::sqrt(delta.m_X * delta.m_X + delta.m_Y * delta.m_Y);
					maxMagnitude = std::max(maxMagnitude, magnitudes[index]);
					const auto& scale = recorded.m_WorldUnitsPerTexel[selected];
					const auto& scaleDelta = recorded.m_TexelScaleDelta[selected];
					const float previousX = scale.m_X - scaleDelta.m_X;
					const float previousY = scale.m_Y - scaleDelta.m_Y;
					const float percentX = previousX > 0.0f ? 100.0f * scaleDelta.m_X / previousX : 0.0f;
					const float percentY = previousY > 0.0f ? 100.0f * scaleDelta.m_Y / previousY : 0.0f;
					scaleChanges[index] = std::sqrt(percentX * percentX + percentY * percentY);
					maxScaleChange = std::max(maxScaleChange, scaleChanges[index]);
					const auto& grid = recorded.m_GridErrorTexels[selected];
					gridErrors[index] = std::sqrt(grid.m_X * grid.m_X + grid.m_Y * grid.m_Y);
				}
				ImGui::PlotLines("Selected cascade |delta| (texels)", magnitudes.data(),
					static_cast<int>(samples.size()), 0, nullptr, 0.0f, maxMagnitude, ImVec2(0.0f, 70.0f));
				ImGui::PlotLines("Selected cascade |scale change| (%)", scaleChanges.data(),
					static_cast<int>(samples.size()), 0, nullptr, 0.0f, maxScaleChange, ImVec2(0.0f, 55.0f));
				ImGui::PlotLines("Selected cascade |grid error| (texels)", gridErrors.data(),
					static_cast<int>(samples.size()), 0, nullptr, 0.0f, 0.75f, ImVec2(0.0f, 55.0f));
			}

			const auto* profiling = context.m_GpuProfiling;
			bool profilingEnabled = profiling && profiling->IsEnabled();
			ImGui::BeginDisabled(!context.m_GpuProfilingControl);
			if (ImGui::Checkbox("GPU profiling", &profilingEnabled) && context.m_GpuProfilingControl)
			{
				context.m_GpuProfilingControl->RequestEnabled(profilingEnabled);
			}
			ImGui::EndDisabled();
			if (profilingEnabled && profiling)
			{
				const auto frame = profiling->GetLatestFrame();
				if (frame.IsValid())
				{
					static constexpr std::array<std::string_view, MaxDirectionalShadowCascades> names = {
						"Shadow.Directional.Cascade0", "Shadow.Directional.Cascade1",
						"Shadow.Directional.Cascade2", "Shadow.Directional.Cascade3",
					};
					ImGui::Text("Last completed GPU frame: %llu",
						static_cast<unsigned long long>(frame.m_FrameIndex));
					for (uint32_t index = 0; shadow && index < shadow->m_Cascades.size() && index < names.size(); ++index)
					{
						const auto sample = std::find_if(frame.m_Samples.begin(), frame.m_Samples.end(),
							[&](const auto& value) { return value.m_Name == names[index]; });
						if (sample != frame.m_Samples.end())
						{
							ImGui::Text("Cascade %u GPU: %.3f ms (%u calls)",
								index, sample->m_Milliseconds, sample->m_CallCount);
						}
						else ImGui::TextDisabled("Cascade %u GPU: waiting for timestamps", index);
					}
				}
				else ImGui::TextDisabled("Waiting for completed GPU timestamps...");
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

			ImGui::TextUnformatted("ShadowMap is a transient RenderGraph texture.");
			ImGui::Text("RG Size: %u | Array layers: %u", snapshot->m_ShadowMapSize,
				snapshot->m_DirectionalShadowMap.m_ArraySize);
			ImGui::Text("Texture Size: %llu x %u",
				static_cast<unsigned long long>(
					snapshot->m_DirectionalShadowMap.m_Extent.m_Width),
				snapshot->m_DirectionalShadowMap.m_Extent.m_Height);
			ImGui::Text("Format: %s",
				GetRHIFormatInfo(snapshot->m_DirectionalShadowMap.m_Format).m_Name);
			ImGui::Text(
				"Preview SRV Format: %s", GetRHIFormatInfo(RHIFormat::R32Float).m_Name);

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
			ImGui::Text("Preview RG Size: %u", snapshot->m_ShadowMapPreviewSize);
			ImGui::Text("Preview layout: %s", overview ? "2x2 cascade overview" : "selected layer");
			if (!overview) ImGui::Text("Selected layer: %d", state.m_SelectedCascade);
			ImGui::Text("Preview Texture Size: %u x %u", preview.m_Width, preview.m_Height);
			ImGui::Text(
				"Preview Format: %s", GetRHIFormatInfo(preview.m_Format).m_Name);
			ImGui::Text("Preview Shader Visible SRV Index: %u", preview.m_SrvDescriptor.m_Index);

			if (!previewTextureId)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"ShadowMap preview SRV GPU handle is invalid.");
				return;
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

		ImGui::TextUnformatted("Shadow Inspector");
		ImGui::Separator();

		DrawShadowCapability(context);
		if (context.m_ShadowVisualizationSettings)
		{
			DrawShadowOverlays(*context.m_ShadowVisualizationSettings);
		}
		DrawLightControl(context);
		DrawTemporalDiagnostics(context, state);
		DrawShadowCamera(context, state);
		DrawShadowPreview(context, state);
	}
}
