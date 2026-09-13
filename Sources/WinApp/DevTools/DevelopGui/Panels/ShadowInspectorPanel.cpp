#include "DevTools/DevelopGui/Panels/ShadowInspectorPanel.h"
#include "GGLabRuntime/Core/Math/Matrix.h"
#include "GGLabRuntime/Scene/DirectionalLightTooling.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiMathWidgets.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/RenderViewSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "GGLabRuntime/Graphics/ShadowPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RenderView.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct ShadowInspectorPanelState
		{
			float m_PreviewSize = 384.0f;
			bool m_FlipPreviewY = false;
			bool m_ShowMatrices = false;
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
			changed |= ImGui::DragFloat(
				"Receiver Depth Bias", &settings.m_ReceiverDepthBias, 0.0001f, 0.0f, 0.1f, "%.5f");
			changed |= ImGui::DragInt(
				"Rasterizer Depth Bias", &settings.m_RasterizerDepthBias, 1.0f, -100000, 100000);
			changed |= ImGui::DragFloat("Slope Scaled Depth Bias", &settings.m_RasterizerSlopeScaledDepthBias,
				0.01f, -100.0f, 100.0f, "%.3f");
			return changed;
		}

		static void DrawVisualizationSettings(ShadowVisualizationSettings& settings) noexcept
		{
			ImGui::SeparatorText("Preview");
			ImGui::DragFloat(
				"Preview Min Depth", &settings.m_PreviewMinDepth, 0.001f, 0.0f, 1.0f, "%.4f");
			ImGui::DragFloat(
				"Preview Max Depth", &settings.m_PreviewMaxDepth, 0.001f, 0.0f, 1.0f, "%.4f");
			settings.m_PreviewMinDepth = std::clamp(settings.m_PreviewMinDepth, 0.0f, 1.0f);
			settings.m_PreviewMaxDepth = std::clamp(settings.m_PreviewMaxDepth, 0.0f, 1.0f);
			if (settings.m_PreviewMaxDepth <= settings.m_PreviewMinDepth)
			{
				settings.m_PreviewMaxDepth = std::min(settings.m_PreviewMinDepth + 0.001f, 1.0f);
			}
			ImGui::Checkbox("Invert Preview", &settings.m_PreviewInvert);
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

			const auto* views = context.m_Diagnostics
				? context.m_Diagnostics->GetSnapshot<RenderViewSnapshot>() : nullptr;
			const RenderView* shadowView = views ? views->FindView(RenderViewID::DirectionalShadow) : nullptr;
			if (!shadowView)
			{
				ImGui::TextColored(devtools::style::ErrorTextColor,
					"Directional shadow render view is not available.");
				return;
			}

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

		static void DrawShadowMapResource(
			DevelopGuiContext& context, ShadowInspectorPanelState& state) noexcept
		{
			ImGui::SeparatorText("ShadowMap Resource");

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
			ImGui::Text("RG Size: %u", snapshot->m_ShadowMapSize);
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

			ImGui::Text("Preview RG Size: %u", snapshot->m_ShadowMapPreviewSize);
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

			ImGui::SeparatorText("Preview");
			ImGui::SliderFloat("Preview Size", &state.m_PreviewSize, 128.0f, 768.0f, "%.0f");
			ImGui::Checkbox("Flip Preview Y", &state.m_FlipPreviewY);

			const float previewSize = std::clamp(state.m_PreviewSize, 16.0f, 2048.0f);
			const ImVec2 uv0 = state.m_FlipPreviewY ? ImVec2(0.0f, 1.0f) : ImVec2(0.0f, 0.0f);
			const ImVec2 uv1 = state.m_FlipPreviewY ? ImVec2(1.0f, 0.0f) : ImVec2(1.0f, 1.0f);
			ImGui::Image(previewTextureId, ImVec2(previewSize, previewSize), uv0, uv1);
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
			DrawVisualizationSettings(*context.m_ShadowVisualizationSettings);
		}
		else
		{
			ImGui::TextColored(devtools::style::ErrorTextColor,
				"Shadow visualization settings are not available.");
		}
		DrawLightControl(context);
		DrawShadowCamera(context, state);
		DrawShadowMapResource(context, state);
	}
}
