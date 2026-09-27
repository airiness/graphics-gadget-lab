#include "DevTools/DevelopGui/Panels/ShadowTemporalLabWidgets.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/ShadowTemporalDiagnostics.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/CameraTooling.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct ShadowTemporalLabState
		{
			int m_SelectedCascade = 0;
			ShadowTemporalDiagnostics m_Temporal;
			CameraEditSettings m_ReplayBase{};
			Vector3 m_ReplayRight = Vector3::UnitX;
			Vector3 m_ReplayForward = Vector3::Forward;
			uint64_t m_ReplayCameraId = 0;
			uint32_t m_ReplayStep = 0;
			bool m_ReplayActive = false;
		};
	}

	void DrawShadowTemporalLabValidation(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ShadowTemporalLabState>();
		ImGui::TextUnformatted("CAM_ShadowStairs reference path");
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
		if (!stairs)
		{
			state.m_Temporal.Reset();
			state.m_ReplayActive = false;
			ImGui::TextDisabled("Available in the Coastal Atrium scene.");
			return;
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
			}
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Stop replay")) state.m_ReplayActive = false;
		ImGui::SameLine();
		if (ImGui::Button("Clear comparison")) state.m_Temporal.Reset();
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
		if (!samples.empty() && samples.back().m_CascadeCount > 0 &&
			shadow && !shadow->m_Cascades.empty() &&
			ImGui::CollapsingHeader("Diagnostics: Cascade Stability"))
		{
			const auto& latest = samples.back();
			if (latest.m_CascadeCount > 1)
			{
				ImGui::SliderInt("Cascade", &state.m_SelectedCascade, 0,
					static_cast<int>(latest.m_CascadeCount) - 1);
			}
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

		if (!ImGui::CollapsingHeader("GPU Timing")) return;
		const auto* profiling = context.m_GpuProfiling;
		const bool profilingEnabled = profiling && profiling->IsEnabled();
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

	void ShadowStairsValidationPanel::Draw(DevelopGuiContext& context) noexcept
	{
		DrawShadowTemporalLabValidation(context);
	}
}
