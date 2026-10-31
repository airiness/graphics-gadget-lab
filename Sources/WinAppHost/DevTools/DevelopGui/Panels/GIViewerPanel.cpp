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
		constexpr std::array Names
		{
			"Irradiance", "Lambert Radiance", "Sun Irradiance", "Environment Irradiance",
			"Base Visibility", "Coat Visibility", "Combined Visibility",
		};
		constexpr auto FirstTap = static_cast<int>(PostProcessDebugTap::GIIrradiance);
		int signal = std::clamp(static_cast<int>(selection.m_Tap) - FirstTap, 0, 6);
		ImGui::BeginDisabled(m_Frozen);
		if (ImGui::Combo("Signal", &signal, Names.data(), static_cast<int>(Names.size())))
		{
			selection.m_Tap = static_cast<PostProcessDebugTap>(FirstTap + signal);
			control->SetPostProcessPreviewSelection(selection, Channel);
		}
		float exposure = preview.m_ExposureEV;
		ImGui::BeginDisabled(signal >= 4);
		if (ImGui::SliderFloat("Preview EV", &exposure, -20.0f, 8.0f, "%.2f"))
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
		ImGui::TextDisabled(signal < 4 ? "Scene referred | ACES + sRGB | independent exposure" :
			"Visibility [0, 1] | black = occluded, white = visible");
		ImGui::TextDisabled("Magenta: invalid | checker: background/pending | blue: inactive lobe");

		const auto& source = preview.m_GISnapshot;
		const int publishedSignal = std::clamp(static_cast<int>(preview.m_Published.m_Tap) - FirstTap, 0, 6);
		ImGui::Text("%s | generation %llu | samples %u / %u",
			m_Frozen ? "Frozen" : "Live", static_cast<unsigned long long>(source.m_Generation),
			source.m_Samples, source.m_TargetSamples);
		if (preview.m_HasPublished && publishedSignal < 4)
		{
			ImGui::SameLine();
			ImGui::Text("| depth %u", source.m_MaximumDepth);
		}
		if (preview.m_HasPublished)
		{
			ImGui::TextDisabled("Last submitted %s: frame %llu | update %llu | %u x %u",
				Names[publishedSignal], static_cast<unsigned long long>(preview.m_FrameSerial),
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
