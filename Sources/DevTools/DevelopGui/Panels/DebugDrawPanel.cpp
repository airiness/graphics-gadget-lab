#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/DebugDrawPanel.h"
#include "Core/Utility/StringUtils.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/DebugDraw/DebugDrawSystem.h"

namespace gglab
{
	namespace
	{
		void DrawStatistics(const DebugDrawStatistics& stats) noexcept
		{
			if (!ImGui::BeginTable("DebugDrawStats", 2,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				return;
			}

			auto row = [](const char* name, uint32_t value) noexcept
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(name);
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u", value);
				};

			row("Submitted commands", stats.m_SubmittedCommandCount);
			row("Accepted commands", stats.m_AcceptedCommandCount);
			row("Invalid commands", stats.m_InvalidCommandCount);
			row("Dropped commands", stats.m_DroppedCommandCount);
			row("Channel-filtered commands", stats.m_ChannelFilteredCommandCount);
			row("Culled commands", stats.m_CulledCommandCount);
			row("Persistent commands", stats.m_PersistentCommandCount);
			row("Line vertices", stats.m_LineVertexCount);
			row("Triangle vertices", stats.m_TriangleVertexCount);

			ImGui::EndTable();
		}

		void DrawChannels(DebugDrawSystem& debugDrawSystem) noexcept
		{
			std::vector<DebugDrawChannelState> channels = debugDrawSystem.GetChannelStates();
			if (channels.empty())
			{
				ImGui::TextDisabled("No DebugDraw channels have been submitted yet.");
				return;
			}

			if (!ImGui::BeginTable("DebugDrawChannels", 5,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				return;
			}
			ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Pending", ImGuiTableColumnFlags_WidthFixed, 70.0f);
			ImGui::TableSetupColumn("Persistent", ImGuiTableColumnFlags_WidthFixed, 80.0f);
			ImGui::TableSetupColumn("Clear", ImGuiTableColumnFlags_WidthFixed, 60.0f);
			ImGui::TableHeadersRow();

			for (const DebugDrawChannelState& channel : channels)
			{
				const std::string name = utils::StringIdToString(channel.m_Channel);
				ImGui::PushID(static_cast<int>(channel.m_Channel.Value() & 0x7fffffffu));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				bool enabled = channel.m_Enabled;
				if (ImGui::Checkbox("##Enabled", &enabled))
				{
					debugDrawSystem.SetChannelEnabled(channel.m_Channel, enabled);
				}
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(name.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::Text("%u", channel.m_PendingCommandCount);
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", channel.m_PersistentCommandCount);
				ImGui::TableSetColumnIndex(4);
				if (ImGui::SmallButton("Clear"))
				{
					debugDrawSystem.ClearChannel(channel.m_Channel);
				}
				ImGui::PopID();
			}

			ImGui::EndTable();
		}
	}

	void DebugDrawPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_DebugDrawSystem)
		{
			ImGui::TextDisabled("DebugDrawSystem is not available.");
			return;
		}

		const DebugDrawFrameView& frame = context.m_DebugDrawFrame;
		ImGui::SeparatorText("Frame");
		DrawStatistics(frame.m_Statistics);

		ImGui::Spacing();
		ImGui::SeparatorText("Channels");
		DrawChannels(*context.m_DebugDrawSystem);
	}
}
