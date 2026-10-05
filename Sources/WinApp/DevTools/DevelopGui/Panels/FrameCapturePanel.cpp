#include "DevTools/DevelopGui/Panels/FrameCapturePanel.h"
#include "Application/Capture/ApplicationFrameCapture.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		constexpr std::chrono::seconds NotificationDuration{ 5 };

		struct FrameCapturePanelState
		{
			std::array<char, 512> m_OutputDirectory{};
			std::array<char, 128> m_Label{};
			std::array<char, 512> m_Note{};
			bool m_Initialized = false;
		};

		template <size_t SIZE>
		void CopyToBuffer(std::array<char, SIZE>& buffer, std::string_view text) noexcept
		{
			const size_t length = std::min(text.size(), SIZE - 1);
			std::memcpy(buffer.data(), text.data(), length);
			buffer[length] = '\0';
		}

		[[nodiscard]] ImVec4 GetGateColor(FrameCaptureGateState state) noexcept
		{
			switch (state)
			{
			case FrameCaptureGateState::Ready:
				return ImVec4(0.45f, 0.85f, 0.45f, 1.0f);
			case FrameCaptureGateState::Failed:
				return ImVec4(0.95f, 0.40f, 0.40f, 1.0f);
			case FrameCaptureGateState::Pending:
				break;
			}
			return ImVec4(0.95f, 0.80f, 0.35f, 1.0f);
		}

		[[nodiscard]] std::string_view GetStatusName(FrameCaptureRequestStatus status) noexcept
		{
			switch (status)
			{
			case FrameCaptureRequestStatus::Completed:
				return "completed";
			case FrameCaptureRequestStatus::Cancelled:
				return "cancelled";
			case FrameCaptureRequestStatus::Failed:
				break;
			}
			return "failed";
		}

		void DrawSettings(ApplicationFrameCapture& frameCapture, FrameCapturePanelState& state) noexcept
		{
			ApplicationFrameCapture::Settings& settings = frameCapture.GetSettings();
			if (!state.m_Initialized)
			{
				CopyToBuffer(state.m_OutputDirectory, settings.m_OutputDirectory.string());
				CopyToBuffer(state.m_Label, settings.m_Label);
				CopyToBuffer(state.m_Note, settings.m_Note);
				state.m_Initialized = true;
			}

			int source = static_cast<int>(settings.m_Source);
			if (ImGui::Combo("Source", &source, "Scene\0Composited\0"))
			{
				settings.m_Source = static_cast<FrameCaptureSource>(source);
			}
			ImGui::SetItemTooltip("Scene: post-processed image without previews or tooling overlays.\n"
				"Composited: final image including debug overlays and this UI.");

			int timing = static_cast<int>(settings.m_Timing);
			if (ImGui::Combo("Timing", &timing, "Next frame\0After ready\0"))
			{
				settings.m_Timing = static_cast<FrameCaptureTiming>(timing);
			}
			if (settings.m_Timing == FrameCaptureTiming::AfterReady)
			{
				constexpr uint32_t minimumSettleFrames = 0;
				constexpr uint32_t maximumSettleFrames = 10000;
				ImGui::DragScalar("Settle Frames", ImGuiDataType_U32, &settings.m_SettleFrames,
					1.0f, &minimumSettleFrames, &maximumSettleFrames);
			}

			if (ImGui::InputText(
				"Output Directory", state.m_OutputDirectory.data(), state.m_OutputDirectory.size()))
			{
				settings.m_OutputDirectory = std::filesystem::path(state.m_OutputDirectory.data());
			}
			ImGui::SetItemTooltip("Empty uses the default directory:\n%s",
				frameCapture.GetDefaultOutputDirectory().string().c_str());
			if (ImGui::InputText("Label", state.m_Label.data(), state.m_Label.size()))
			{
				settings.m_Label = state.m_Label.data();
			}
			if (ImGui::InputText("Note", state.m_Note.data(), state.m_Note.size()))
			{
				settings.m_Note = state.m_Note.data();
			}

			if (ImGui::Button("Capture (F9)"))
			{
				frameCapture.Capture();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%u pending", frameCapture.GetUnfinishedRequestCount());
		}

		void DrawFrameState(const ApplicationFrameCapture& frameCapture) noexcept
		{
			const FrameCaptureFrameState* frameState = frameCapture.GetLastFrameState();
			if (!frameState)
			{
				ImGui::TextDisabled("No frame has been rendered yet.");
				return;
			}

			ImGui::Text("Backend: %s", frameState->m_Backend.c_str());
			ImGui::Text("Demo: %s", frameState->m_DemoId.c_str());
			ImGui::Text("Lab: %s",
				frameState->m_LabId.empty() ? "-" : frameState->m_LabId.c_str());
			ImGui::Text("Camera: %s (%.2f, %.2f, %.2f), %.1f deg",
				frameState->m_Camera.m_Name.c_str(), frameState->m_Camera.m_Position[0],
				frameState->m_Camera.m_Position[1], frameState->m_Camera.m_Position[2],
				frameState->m_Camera.m_VerticalFovDegrees);
			const std::string timeStep = frameState->m_FixedDeltaTime
				? std::format("fixed {:.4f} s", *frameState->m_FixedDeltaTime)
				: std::string("wall clock");
			ImGui::Text("Time step: %s", timeStep.c_str());

			const bool ready = frameState->m_Readiness.IsReady();
			ImGui::TextColored(GetGateColor(ready ? FrameCaptureGateState::Ready
				: frameState->m_Readiness.HasFailed() ? FrameCaptureGateState::Failed
				: FrameCaptureGateState::Pending),
				"Readiness: %s, %u settled frames", ready ? "ready" : "not ready",
				frameCapture.GetSettledFrameCount());

			constexpr ImGuiTableFlags flags = ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
			if (ImGui::BeginTable("##FrameCaptureGates", 3, flags))
			{
				ImGui::TableSetupColumn("Gate");
				ImGui::TableSetupColumn("State");
				ImGui::TableSetupColumn("Detail");
				ImGui::TableHeadersRow();
				for (const FrameCaptureGate& gate : frameState->m_Readiness.m_Gates)
				{
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(gate.m_Name.c_str());
					ImGui::TableNextColumn();
					ImGui::TextColored(GetGateColor(gate.m_State), "%s",
						GetFrameCaptureGateStateName(gate.m_State).data());
					ImGui::TableNextColumn();
					ImGui::TextUnformatted(gate.m_Detail.c_str());
				}
				ImGui::EndTable();
			}
		}

		void DrawHistory(const ApplicationFrameCapture& frameCapture) noexcept
		{
			const auto& history = frameCapture.GetHistory();
			if (history.empty())
			{
				ImGui::TextDisabled("No finished captures.");
				return;
			}
			for (auto entry = history.rbegin(); entry != history.rend(); ++entry)
			{
				const FrameCaptureRequestResult& result = entry->m_Result;
				ImGui::PushID(static_cast<int>(result.m_RequestId));
				const bool completed = result.m_Status == FrameCaptureRequestStatus::Completed;
				ImGui::TextColored(GetGateColor(completed ? FrameCaptureGateState::Ready
					: FrameCaptureGateState::Failed),
					"#%llu %s", static_cast<unsigned long long>(result.m_RequestId),
					GetStatusName(result.m_Status).data());
				if (result.m_Metadata)
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s, frame %llu",
						GetFrameCaptureSourceName(result.m_Metadata->m_Source).data(),
						static_cast<unsigned long long>(result.m_Metadata->m_FrameSerial));
				}
				ImGui::Indent();
				if (completed)
				{
					const std::string path = result.m_ImagePath.string();
					if (ImGui::Selectable(path.c_str()))
					{
						ImGui::SetClipboardText(path.c_str());
					}
					ImGui::SetItemTooltip("Click to copy the image path.");
				}
				else
				{
					ImGui::PushTextWrapPos();
					ImGui::TextUnformatted(result.m_Failure.c_str());
					ImGui::PopTextWrapPos();
				}
				ImGui::Unindent();
				ImGui::PopID();
			}
		}
	}

	void FrameCapturePanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!m_FrameCapture)
		{
			ImGui::TextDisabled("Frame capture is not available.");
			return;
		}

		auto& state = context.PanelState<FrameCapturePanelState>();
		if (ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawSettings(*m_FrameCapture, state);
		}
		if (ImGui::CollapsingHeader("Current Frame", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawFrameState(*m_FrameCapture);
		}
		if (ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen))
		{
			DrawHistory(*m_FrameCapture);
		}
	}

	void DrawFrameCaptureNotification(const ApplicationFrameCapture& frameCapture) noexcept
	{
		const auto& history = frameCapture.GetHistory();
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (history.empty() || !viewport ||
			std::chrono::steady_clock::now() - history.back().m_FinishedAt > NotificationDuration)
		{
			return;
		}

		const FrameCaptureRequestResult& result = history.back().m_Result;
		const bool completed = result.m_Status == FrameCaptureRequestStatus::Completed;
		const ImVec2 position(viewport->WorkPos.x + viewport->WorkSize.x - 16.0f,
			viewport->WorkPos.y + viewport->WorkSize.y - 16.0f);
		ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
			ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
			ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
		if (ImGui::Begin("##FrameCaptureNotification", nullptr, flags))
		{
			ImGui::TextColored(GetGateColor(completed ? FrameCaptureGateState::Ready
				: FrameCaptureGateState::Failed),
				"Capture #%llu %s", static_cast<unsigned long long>(result.m_RequestId),
				GetStatusName(result.m_Status).data());
			ImGui::PushTextWrapPos(ImGui::GetFontSize() * 40.0f);
			ImGui::TextUnformatted(
				completed ? result.m_ImagePath.string().c_str() : result.m_Failure.c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::End();
	}
}
