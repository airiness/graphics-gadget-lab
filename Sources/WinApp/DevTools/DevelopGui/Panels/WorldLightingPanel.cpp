#include "DevTools/DevelopGui/Panels/WorldLightingPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AtmosphereDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Scene/DirectionalLightTooling.h"

#include <algorithm>
#include <optional>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		void DrawSun(DevelopGuiContext& context) noexcept
		{
			const auto light = context.m_DirectionalLight
				? context.m_DirectionalLight->GetLight() : std::nullopt;
			if (!light)
			{
				ImGui::TextDisabled("No directional light.");
				return;
			}
			ImGui::Text("Sun: %s", light->m_WorldSun ? "Physical" : "Artistic");
			auto* control = context.m_DirectionalLightControl;
			ImGui::BeginDisabled(!control);
			float direction[3] = { light->m_Direction.m_X, light->m_Direction.m_Y,
				light->m_Direction.m_Z };
			if (ImGui::DragFloat3("Direction", direction, 0.01f, -1.0f, 1.0f, "%.3f") && control)
			{
				control->SetDirection(light->m_Id, Vector3(direction[0], direction[1], direction[2]));
			}
			bool physical = light->m_WorldSun.has_value();
			if (ImGui::Checkbox("Physical Sun", &physical) && control)
			{
				control->SetWorldSun(light->m_Id, physical
					? std::optional<WorldSunSettings>(WorldSunSettings{}) : std::nullopt);
			}
			if (light->m_WorldSun)
			{
				auto settings = *light->m_WorldSun;
				bool changed = ImGui::DragFloat("TOA Illuminance (lux)",
					&settings.m_PerpendicularIlluminanceLux, 100.0f, 0.0f, 1000000.0f, "%.0f");
				changed |= ImGui::DragFloat("Angular Radius (degrees)",
					&settings.m_AngularRadiusDegrees, 0.001f, 0.01f, 5.0f, "%.4f");
				float chromaticity[3] = { settings.m_Chromaticity.m_X,
					settings.m_Chromaticity.m_Y, settings.m_Chromaticity.m_Z };
				if (ImGui::CollapsingHeader("Advanced: Solar Color"))
				{
					changed |= ImGui::ColorEdit3("Chromaticity (linear)", chromaticity);
				}
				if (changed && control)
				{
					settings.m_Chromaticity = Vector3(chromaticity[0], chromaticity[1], chromaticity[2]);
					control->SetWorldSun(light->m_Id, settings);
				}
				if (light->m_ResolvedWorldSun)
				{
					ImGui::Text("Resolved illuminance: %.0f lux",
						light->m_ResolvedWorldSun->m_PerpendicularIlluminanceLux);
				}
			}
			else
			{
				float color[3] = { light->m_Color.m_R, light->m_Color.m_G, light->m_Color.m_B };
				bool changed = ImGui::ColorEdit3("Color", color);
				float intensity = light->m_Intensity;
				changed |= ImGui::DragFloat("Artistic Intensity", &intensity, 0.01f, 0.0f, 100.0f);
				if (changed && control)
				{
					auto radiance = light->m_Color;
					radiance.m_R = color[0];
					radiance.m_G = color[1];
					radiance.m_B = color[2];
					control->SetRadiance(light->m_Id, radiance, intensity);
				}
			}
			ImGui::EndDisabled();
		}

		void DrawAtmosphere(DevelopGuiContext& context) noexcept
		{
			const auto light = context.m_DirectionalLight
				? context.m_DirectionalLight->GetLight() : std::nullopt;
			if (!light)
			{
				ImGui::TextDisabled("No directional light.");
				return;
			}
			ImGui::Text("Atmosphere: %s",
				light->m_Atmosphere ? "Enabled" : "Disabled");
			auto* control = context.m_DirectionalLightControl;
			ImGui::BeginDisabled(!control);
			bool enabled = light->m_Atmosphere.has_value();
			if (ImGui::Checkbox("Enabled", &enabled) && control)
			{
				control->SetAtmosphere(enabled
					? std::optional<AtmosphereSettings>(AtmosphereSettings{}) : std::nullopt);
			}
			if (light->m_Atmosphere)
			{
				auto settings = *light->m_Atmosphere;
				bool changed = ImGui::DragFloat("Atmosphere Height (m)",
					&settings.m_HeightMeters, 100.0f, 1000.0f, 1000000.0f);
				changed |= ImGui::DragFloat("Rayleigh Scale Height (m)",
					&settings.m_RayleighScaleHeightMeters, 50.0f, 100.0f, 100000.0f);
				changed |= ImGui::DragFloat("Mie Scale Height (m)",
					&settings.m_MieScaleHeightMeters, 10.0f, 100.0f, 100000.0f);
				changed |= ImGui::SliderFloat("Mie Anisotropy",
					&settings.m_MieAnisotropy, -0.95f, 0.95f);
				if (ImGui::CollapsingHeader("Advanced: Atmosphere Scale"))
				{
					changed |= ImGui::DragFloat("Absorption Center (m)",
						&settings.m_AbsorptionCenterMeters, 100.0f, 0.0f,
						settings.m_HeightMeters);
					changed |= ImGui::DragFloat("Meters per World Unit",
						&settings.m_WorldUnitsToMeters, 0.01f, 0.0001f, 10000.0f);
				}
				if (changed && control) control->SetAtmosphere(settings);
				if (ImGui::Button("Restore Earth") && control)
				{
					control->SetAtmosphere(AtmosphereSettings{});
				}
			}
			ImGui::EndDisabled();
			if (context.m_EnvironmentLighting)
			{
				const auto environment = context.m_EnvironmentLighting->GetEnvironmentLightingSettings();
				const bool previewSky = environment.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalAtmospherePreview;
				const bool physicalSky = environment.m_BackgroundMode == EnvironmentBackgroundMode::PhysicalSky;
				auto* environmentControl = context.m_EnvironmentLightingControl;
				ImGui::BeginDisabled(!environmentControl);
				int sourceMode = static_cast<int>(environment.m_BackgroundMode);
				if (ImGui::Combo("Sky Source", &sourceMode,
					"HDR Texture\0Physical Sky Preview\0Physical Sky\0") && environmentControl)
				{
					environmentControl->SetBackgroundMode(static_cast<EnvironmentBackgroundMode>(sourceMode));
				}
				ImGui::EndDisabled();
				if (previewSky && !environment.m_EnableSkybox)
				{
					ImGui::TextDisabled("Preview paused: enable Skybox in Environment & IBL.");
				}
				else if (previewSky && (!light->m_Atmosphere || !light->m_WorldSun))
				{
					ImGui::TextDisabled("Preview paused: enable Atmosphere and Physical Sun.");
				}
				else if (previewSky)
				{
					ImGui::TextDisabled("Display preview only; IBL still uses the HDR texture.");
				}
				else if (physicalSky)
				{
					ImGui::TextDisabled("Sun, sky and IBL switch together when the replacement is ready.");
					if (!light->m_Atmosphere || !light->m_WorldSun)
						ImGui::TextDisabled("Physical Sky requires Atmosphere and Physical Sun.");
				}
			}

			const auto* snapshot = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<AtmosphereDiagnosticsSnapshot>() : nullptr;
			if (snapshot)
			{
				ImGui::Text("Active source: %s", snapshot->m_PhysicalSkyActive ? "Physical Sky" : "HDR Texture");
				ImGui::Text("World lighting: active %llu | requested %llu",
					static_cast<unsigned long long>(snapshot->m_ActiveWorldLightingGeneration),
					static_cast<unsigned long long>(snapshot->m_RequestedWorldLightingGeneration));
				ImGui::Text("Last publication: %.1f ms | retiring persistent textures: %u",
					snapshot->m_PublicationMilliseconds, snapshot->m_RetiringTextureCount);
				if (snapshot->m_PhysicalSkyActive)
				{
					ImGui::Text("IBL reference altitude: %.0f m | range %.0f-%.0f m",
						snapshot->m_ReferenceObserverAltitudeMeters, snapshot->m_ObserverMinAltitudeMeters,
						snapshot->m_ObserverMaxAltitudeMeters);
					ImGui::TextDisabled("Local +Y region within 1 km; camera motion does not rebake IBL.");
					if (snapshot->m_ActiveSun)
					{
						const auto& sun = *snapshot->m_ActiveSun;
						ImGui::Text("Active sun: %.0f lux | direction %.3f, %.3f, %.3f",
							sun.m_PerpendicularIlluminanceLux, sun.m_Direction.m_X,
							sun.m_Direction.m_Y, sun.m_Direction.m_Z);
					}
				}
			}
			if (snapshot && light->m_Atmosphere)
			{
				constexpr auto Channel = PostProcessPreviewChannel::Atmosphere;
				static constexpr const char* Names[] = {
					"Transmittance LUT", "Multiple Scattering LUT", "Sky View LUT",
					"Aerial Transmittance", "Aerial In-Scattering"
				};
				const auto* view = context.m_PostProcessPreview;
				auto* previewControl = context.m_PostProcessPreviewControl;
				if (view)
				{
					const auto preview = view->GetPostProcessPreviewDiagnostics(Channel);
					static constexpr PostProcessDebugTap Taps[] = {
						PostProcessDebugTap::AtmosphereTransmittance,
						PostProcessDebugTap::AtmosphereMultipleScattering,
						PostProcessDebugTap::AtmosphereSkyView,
						PostProcessDebugTap::AtmosphereAerialTransmittance,
						PostProcessDebugTap::AtmosphereAerialInScattering
					};
					PostProcessDebugSelection selection = preview.m_Selected;
					const int selectedIndex = static_cast<int>(selection.m_Tap) -
						static_cast<int>(PostProcessDebugTap::AtmosphereTransmittance);
					ImGui::BeginDisabled(!previewControl);
					if (ImGui::BeginCombo("Preview", Names[std::clamp(selectedIndex, 0, 4)]))
					{
						for (int index = 0; index < 5; ++index)
						{
							if (ImGui::Selectable(Names[index], selectedIndex == index))
							{
								selection.m_Tap = Taps[index];
								previewControl->SetPostProcessPreviewSelection(selection, Channel);
							}
						}
						ImGui::EndCombo();
					}
					float exposureEV = preview.m_ExposureEV;
					if (ImGui::SliderFloat("Preview Exposure", &exposureEV,
						-8.0f, 8.0f, "%+.2f EV") && previewControl)
					{
						previewControl->SetPostProcessPreviewExposureEV(exposureEV, Channel);
					}
					ImGui::EndDisabled();
					if (previewControl) previewControl->RequestPostProcessPreview(Channel);
					if (preview.m_HasPublished && preview.m_Published == selection &&
						preview.m_Width && preview.m_Height)
					{
						const ImTextureID textureId = devtools::ResolveImGuiTextureId(
							context.m_DevelopGuiSystem, preview.m_SrvDescriptor);
						if (textureId)
						{
							const float width = std::clamp(ImGui::GetContentRegionAvail().x, 64.0f, 768.0f);
							ImGui::Image(textureId, ImVec2(width,
								width * static_cast<float>(preview.m_Height) / preview.m_Width));
						}
						ImGui::TextDisabled("%s | frame %llu | update %llu",
							Names[std::clamp(selectedIndex, 0, 4)],
							static_cast<unsigned long long>(preview.m_FrameSerial),
							static_cast<unsigned long long>(preview.m_UpdateCount));
					}
					else ImGui::TextDisabled("Atmosphere preview pending.");
				}
				if (ImGui::CollapsingHeader("Diagnostics: Atmosphere GPU"))
				{
					ImGui::Text("Updating: %s%s%s",
						(snapshot->m_State.m_DirtyMask & 1u) ? "Transmittance " : "",
						(snapshot->m_State.m_DirtyMask & 2u) ? "Multiple Scattering " : "",
						(snapshot->m_State.m_DirtyMask & 4u) ? "Sky View" : "");
					for (uint32_t index = 0; index < 3; ++index)
					{
						ImGui::Text("%s: generation %llu", Names[index],
							static_cast<unsigned long long>(snapshot->m_State.m_Generations[index]));
					}
					if (!snapshot->m_GpuPasses.empty())
					{
						ImGui::Text("Latest captured GPU frame: %llu",
							static_cast<unsigned long long>(snapshot->m_GpuFrameIndex));
						for (const auto& pass : snapshot->m_GpuPasses)
						{
							ImGui::Text("%s: %.3f ms", pass.m_Name.c_str(), pass.m_Milliseconds);
						}
					}
					else ImGui::TextDisabled("Atmosphere timings: enable Diagnostics / Profiling.");
				}
			}
		}
	}

	void WorldLightingPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!ImGui::BeginTabBar("WorldLightingTabs")) return;
		if (ImGui::BeginTabItem("Sun"))
		{
			DrawSun(context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Atmosphere"))
		{
			DrawAtmosphere(context);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Environment & IBL"))
		{
			m_EnvironmentPanel.Draw(context);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}
