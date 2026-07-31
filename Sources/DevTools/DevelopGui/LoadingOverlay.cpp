#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/LoadingOverlay.h"
#include "Application/LoadingProgress.h"

#include <imgui.h>

namespace gglab
{
	void DrawLoadingOverlay(const LoadingProgress& progress) noexcept
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (!viewport)
		{
			return;
		}

		const float fraction = std::clamp(progress.m_Fraction, 0.0f, 1.0f);
		const int32_t percentage = static_cast<int32_t>(std::round(fraction * 100.0f));
		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
			viewport->WorkPos.y + viewport->WorkSize.y * 0.78f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::SetNextWindowSize(
			ImVec2(std::clamp(viewport->WorkSize.x * 0.48f, 420.0f, 720.0f), 0.0f));
		ImGui::SetNextWindowBgAlpha(0.92f);

		constexpr ImGuiWindowFlags flags =
			ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs;
		if (ImGui::Begin("##LoadingOverlay", nullptr, flags))
		{
			const char* title = progress.m_Title.empty() ? "Loading" : progress.m_Title.c_str();
			ImGui::TextUnformatted(title);
			const std::string percentageText = std::format("{}%", percentage);
			ImGui::ProgressBar(fraction, ImVec2(-FLT_MIN, 0.0f), percentageText.c_str());
			if (!progress.m_Stage.empty())
			{
				ImGui::TextUnformatted(progress.m_Stage.c_str());
			}
			if (!progress.m_Detail.empty())
			{
				ImGui::PushTextWrapPos();
				ImGui::TextDisabled("%s", progress.m_Detail.c_str());
				ImGui::PopTextWrapPos();
			}
		}
		ImGui::End();
	}
}
