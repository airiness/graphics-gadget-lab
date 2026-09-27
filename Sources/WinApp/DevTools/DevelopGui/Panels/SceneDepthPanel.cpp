#include "DevTools/DevelopGui/Panels/SceneDepthPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiTextureUtils.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/SceneDepthDiagnosticsSnapshot.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewControlBase.h"
#include "GGLabRuntime/Graphics/PostProcess/PostProcessPreviewViewBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"

#include <algorithm>

#include <imgui.h>

namespace gglab
{
	void SceneDepthPanel::Draw(DevelopGuiContext& context) noexcept
	{
		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<SceneDepthDiagnosticsSnapshot>() : nullptr;
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("Scene depth is unavailable.");
			return;
		}
		const auto& depth = *snapshot;
		ImGui::Text("%u x %u | %s", depth.m_Width, depth.m_Height,
			depth.m_Convention == DepthConvention::Reversed ? "Reversed-Z" : "Standard-Z");
		if (const auto* view = context.m_PostProcessPreview)
		{
			constexpr auto Channel = PostProcessPreviewChannel::SceneDepth;
			const auto preview = view->GetPostProcessPreviewDiagnostics(Channel);
			PostProcessDebugSelection selection = preview.m_Selected;
			bool linear = selection.m_Tap == PostProcessDebugTap::SceneDepthLinearViewZ;
			auto* control = context.m_PostProcessPreviewControl;
			ImGui::BeginDisabled(!control);
			if (ImGui::Checkbox("Linear View Z", &linear) && control)
			{
				selection.m_Tap = linear ? PostProcessDebugTap::SceneDepthLinearViewZ
					: PostProcessDebugTap::SceneDepthRaw;
				control->SetPostProcessPreviewSelection(selection, Channel);
			}
			ImGui::EndDisabled();
			if (control) control->RequestPostProcessPreview(Channel);
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
					linear ? "Linear View Z" : "Raw Depth",
					static_cast<unsigned long long>(preview.m_FrameSerial),
					static_cast<unsigned long long>(preview.m_UpdateCount));
			}
			else ImGui::TextDisabled("Depth preview pending.");
		}
		if (ImGui::CollapsingHeader("Diagnostics: Resource"))
		{
			ImGui::Text("Resource / DSV / SRV: %s / %s / %s",
				GetRHIFormatInfo(depth.m_ResourceFormat).m_Name,
				GetRHIFormatInfo(depth.m_DsvFormat).m_Name,
				GetRHIFormatInfo(depth.m_SrvFormat).m_Name);
			ImGui::Text("Clear: %.3f (%s)", depth.m_ClearDepth,
				depth.m_HasTypedClear ? "typed" : "none");
		}
	}
}
