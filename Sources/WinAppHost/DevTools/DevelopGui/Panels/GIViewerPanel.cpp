#include "DevTools/DevelopGui/Panels/GIViewerPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"

#include <algorithm>
#include <array>
#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct SignalOption
		{
			PostProcessDebugTap m_Tap;
			const char* m_Name;
		};
		constexpr std::array SignalOptions
		{
			SignalOption{ PostProcessDebugTap::GIIrradiance, "Irradiance" },
			SignalOption{ PostProcessDebugTap::GILambertRadiance, "Lambert Radiance" },
			SignalOption{ PostProcessDebugTap::GISunIrradiance, "Sun Irradiance" },
			SignalOption{ PostProcessDebugTap::GIEnvironmentIrradiance, "Environment Irradiance" },
			SignalOption{ PostProcessDebugTap::GIBaseVisibility, "Base Visibility" },
			SignalOption{ PostProcessDebugTap::GICoatVisibility, "Coat Visibility" },
			SignalOption{ PostProcessDebugTap::GICombinedVisibility, "Combined Visibility" },
		};
		const SignalOption& FindSignalOption(PostProcessDebugTap tap) noexcept
		{
			const auto found = std::ranges::find(SignalOptions, tap, &SignalOption::m_Tap);
			return found != SignalOptions.end() ? *found : SignalOptions.front();
		}
	}

	void GIViewerPanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::SetWindowPos(ImVec2(460, 45), ImGuiCond_FirstUseEver);
		ImGui::SetWindowSize(ImVec2(760, 660), ImGuiCond_FirstUseEver);
		const auto* view = context.m_PostProcessPreview;
		auto* control = context.m_PostProcessPreviewControl;
		if (!view || !control)
		{
			ImGui::TextDisabled("GI preview is unavailable.");
			return;
		}
		constexpr auto Channel = PostProcessPreviewChannel::GlobalIllumination;
		const auto preview = view->GetPostProcessPreviewDiagnostics(Channel);
		auto selection = preview.m_Selected;
		ImGui::BeginDisabled(m_Frozen);
		if (ImGui::BeginCombo("Signal", FindSignalOption(selection.m_Tap).m_Name))
		{
			for (const auto& option : SignalOptions)
			{
				const bool selected = option.m_Tap == selection.m_Tap;
				if (ImGui::Selectable(option.m_Name, selected))
				{
					selection.m_Tap = option.m_Tap;
					control->SetPostProcessPreviewSelection(selection, Channel);
				}
				if (selected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		const bool visibility = IsGIVisibilityPreviewTap(selection.m_Tap);
		float exposure = preview.m_ExposureEV;
		ImGui::BeginDisabled(visibility);
		if (ImGui::SliderFloat("Preview EV", &exposure,
			GetPostProcessPreviewMinimumExposureEV(Channel), PostProcessPreviewMaximumExposureEV, "%.2f"))
		{
			control->SetPostProcessPreviewExposureEV(exposure, Channel);
		}
		ImGui::EndDisabled();
		ImGui::EndDisabled();
		ImGui::Checkbox("Freeze preview", &m_Frozen);
		ImGui::SameLine();
		ImGui::TextDisabled("Sampling continues");
		if (!m_Frozen)
		{
			control->RequestPostProcessPreview(Channel);
		}
		ImGui::TextDisabled(!visibility ? "Scene referred | ACES + sRGB | independent exposure" :
			"Visibility [0, 1] | black = occluded, white = visible");
		ImGui::TextDisabled("Magenta: invalid | checker: background/pending | blue: inactive lobe");

		const auto& source = preview.m_GISnapshot;
		const auto& publishedSignal = FindSignalOption(preview.m_Published.m_Tap);
		ImGui::Text("%s | generation %llu | samples %u / %u",
			m_Frozen ? "Frozen" : "Live", static_cast<unsigned long long>(source.m_Generation),
			source.m_Samples, source.m_TargetSamples);
		if (preview.m_HasPublished && IsGIDiffusePreviewTap(preview.m_Published.m_Tap))
		{
			ImGui::SameLine();
			ImGui::Text("| depth %u", source.m_MaximumDepth);
		}
		if (preview.m_HasPublished)
		{
			ImGui::TextDisabled("Last submitted %s: frame %llu | update %llu | %u x %u",
				publishedSignal.m_Name, static_cast<unsigned long long>(preview.m_FrameSerial),
				static_cast<unsigned long long>(preview.m_UpdateCount), preview.m_Width, preview.m_Height);
		}
		else
		{
			ImGui::TextDisabled("Waiting for a submitted preview.");
		}
		// The graph may already be recording the next live update. The provenance above
		// intentionally describes the last submitted preview, not a GPU-completion claim.
		ImGui::BeginChild("GIImage", ImVec2(0, 0), ImGuiChildFlags_Borders);
		if (preview.m_SourceAvailable && preview.m_HasPublished && preview.m_Published == selection &&
			preview.m_Width && preview.m_Height)
		{
			const ImTextureID texture = devtools::ResolveImGuiTextureId(
				context.m_DevelopGuiSystem, preview.m_SrvDescriptor);
			if (texture)
			{
				const ImVec2 available = ImGui::GetContentRegionAvail();
				const float aspect = static_cast<float>(preview.m_Width) / preview.m_Height;
				const float width = std::max(1.0f, std::min(available.x, available.y * aspect));
				ImGui::Image(texture, ImVec2(width, width / aspect));
			}
		}
		else
		{
			ImGui::TextWrapped("No preview yet. Open GI Reference in Lab Control and select a matching "
				"fixture. Visibility requires a specular-visibility fixture. Unfreeze to request an update.");
		}
		ImGui::EndChild();
	}
}
