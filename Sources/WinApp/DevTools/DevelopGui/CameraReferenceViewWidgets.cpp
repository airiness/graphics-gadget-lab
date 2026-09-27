#include "DevTools/DevelopGui/CameraReferenceViewWidgets.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/CameraTooling.h"

#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace gglab
{
	bool ReferenceViewsBelongInLabPanel(const LabSnapshot& lab) noexcept
	{
		return lab.m_IsHostActive && lab.m_ActiveLabId.m_Name == "gglab.lab.lighting_contract";
	}

	std::optional<uint64_t> DrawCameraReferenceViews(CameraReferenceViewWidgetState& state,
		const CameraToolingSnapshot& snapshot, CameraToolingControlBase* control) noexcept
	{
		if (snapshot.m_ReferenceViews.empty()) return std::nullopt;
		const auto main = std::ranges::find(snapshot.m_Cameras, RenderViewID::Main,
			&CameraToolingObservation::m_RenderViewId);
		if (main == snapshot.m_Cameras.end()) return std::nullopt;
		if (state.m_MainCameraId != main->m_Id)
		{
			state.m_MainCameraId = main->m_Id;
			state.m_SelectedReferenceId = snapshot.m_LastRestoredReferenceId;
		}
		auto selected = std::ranges::find(snapshot.m_ReferenceViews, state.m_SelectedReferenceId,
			&CameraReferenceView::m_Id);
		if (selected == snapshot.m_ReferenceViews.end())
		{
			selected = snapshot.m_ReferenceViews.begin();
			state.m_SelectedReferenceId = selected->m_Id;
		}
		std::optional<uint64_t> restoredCamera;
		const auto restore = [&](const CameraReferenceView& reference) noexcept
			{
				if (control && control->RestoreReferenceView(main->m_Id, reference.m_Id))
				{
					state.m_SelectedReferenceId = reference.m_Id;
					restoredCamera = main->m_Id;
				}
			};
		ImGui::SeparatorText("Reference Views");
		ImGui::BeginDisabled(!control);
		if (ImGui::BeginCombo("Reference View", selected->m_Name.c_str()))
		{
			for (const auto& reference : snapshot.m_ReferenceViews)
			{
				const bool current = reference.m_Id == state.m_SelectedReferenceId;
				if (ImGui::Selectable(reference.m_Name.c_str(), current)) restore(reference);
				if (current) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}
		selected = std::ranges::find(snapshot.m_ReferenceViews, state.m_SelectedReferenceId,
			&CameraReferenceView::m_Id);
		if (ImGui::Button("Restore Reference View")) restore(*selected);
		ImGui::EndDisabled();
		ImGui::TextWrapped("%s", selected->m_Purpose.c_str());
		ImGui::TextDisabled("%s / profile %u", selected->m_Id.c_str(), selected->m_ProfileVersion);
		if (std::abs(main->m_Aspect - selected->m_ReferenceAspect) > 0.001f)
		{
			ImGui::TextWrapped("Composition aspect: %.4f; current viewport: %.4f.",
				selected->m_ReferenceAspect, main->m_Aspect);
		}
		ImGui::Spacing();
		return restoredCamera;
	}
}
