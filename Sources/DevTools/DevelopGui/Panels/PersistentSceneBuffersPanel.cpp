#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/PersistentSceneBuffersPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/PersistentSceneBufferSnapshot.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	namespace
	{
		struct PersistentSceneBuffersPanelState
		{
			bool m_HideFreeSlots = true;
		};

		void DrawVersions(const PersistentBufferTableSnapshot& table) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable;
			if (!ImGui::BeginTable("BufferVersions", 8, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Version");
			ImGui::TableSetupColumn("Handle");
			ImGui::TableSetupColumn("Pending Slots");
			ImGui::TableSetupColumn("Pending Ranges");
			ImGui::TableSetupColumn("Pending Bytes");
			ImGui::TableSetupColumn("Last Ranges");
			ImGui::TableSetupColumn("Last Bytes");
			ImGui::TableSetupColumn("Total Bytes");
			ImGui::TableHeadersRow();
			for (const auto& version : table.m_BufferVersions)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::Text("%u", version.m_BufferIndex);
				ImGui::TableSetColumnIndex(1); ImGui::Text("%u:%u", version.m_Buffer.Index(), version.m_Buffer.Generation());
				ImGui::TableSetColumnIndex(2); ImGui::Text("%u", version.m_PendingSlotCount);
				ImGui::TableSetColumnIndex(3); ImGui::Text("%u", version.m_PendingRangeCount);
				ImGui::TableSetColumnIndex(4); ImGui::Text("%llu", static_cast<unsigned long long>(version.m_PendingUploadBytes));
				ImGui::TableSetColumnIndex(5); ImGui::Text("%u", version.m_LastUploadRangeCount);
				ImGui::TableSetColumnIndex(6); ImGui::Text("%llu", static_cast<unsigned long long>(version.m_LastUploadBytes));
				ImGui::TableSetColumnIndex(7); ImGui::Text("%llu", static_cast<unsigned long long>(version.m_TotalUploadBytes));
			}
			ImGui::EndTable();
		}

		void DrawSlots(
			const PersistentBufferTableSnapshot& table,
			bool hideFreeSlots) noexcept
		{
			const int columnCount = 4 + static_cast<int>(table.m_BufferVersions.size());
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("Slots", columnCount, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Key");
			ImGui::TableSetupColumn("CPU Revision");
			for (const auto& version : table.m_BufferVersions)
			{
				const std::string label = std::format("GPU v{}", version.m_BufferIndex);
				ImGui::TableSetupColumn(label.c_str());
			}
			ImGui::TableHeadersRow();

			for (const auto& slot : table.m_Slots)
			{
				if (hideFreeSlots && !slot.m_Occupied)
				{
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::Text("%u", slot.m_Slot);
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(slot.m_Occupied ? "Live" : "Free");
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(slot.m_Key.empty() ? "-" : slot.m_Key.c_str());
				ImGui::TableSetColumnIndex(3); ImGui::Text("%llu", static_cast<unsigned long long>(slot.m_Revision));
				for (uint32_t index = 0; index < slot.m_UploadedRevisions.size(); ++index)
				{
					const uint64_t uploaded = slot.m_UploadedRevisions[index];
					ImGui::TableSetColumnIndex(4 + static_cast<int>(index));
					if (slot.m_Occupied && uploaded != slot.m_Revision)
					{
						ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "%llu pending",
							static_cast<unsigned long long>(uploaded));
					}
					else
					{
						ImGui::Text("%llu", static_cast<unsigned long long>(uploaded));
					}
				}
			}
			ImGui::EndTable();
		}

		void DrawTable(
			const PersistentBufferTableSnapshot& table,
			bool hideFreeSlots) noexcept
		{
			ImGui::PushID(table.m_Name.c_str());
			ImGui::Text("%s: %u live / %u capacity | %u free | stride %u bytes | update %llu",
				table.m_Name.c_str(), table.m_LiveCount, table.m_Capacity, table.m_FreeCount,
				table.m_ElementStride, static_cast<unsigned long long>(table.m_UpdateSerial));
			ImGui::TextUnformatted("Memory: GpuOnly | Update path: upload staging -> copy queue -> GPU buffer");
			DrawVersions(table);
			ImGui::Separator();
			DrawSlots(table, hideFreeSlots);
			ImGui::PopID();
		}
	}

	void PersistentSceneBuffersPanel::Draw(DevelopGuiContext& context) noexcept
	{
		if (!context.m_Renderer)
		{
			ImGui::TextDisabled("Renderer is not available.");
			return;
		}

		auto& state = context.PanelState<PersistentSceneBuffersPanelState>();
		const auto* snapshot = context.m_Diagnostics ?
			context.m_Diagnostics->GetSnapshot<PersistentSceneBufferSnapshot>() : nullptr;
		if (!snapshot)
		{
			ImGui::TextDisabled("Persistent buffer snapshot provider is not available.");
			return;
		}
		ImGui::Checkbox("Hide Free Slots", &state.m_HideFreeSlots);

		if (ImGui::BeginTabBar("PersistentSceneBufferTabs"))
		{
			if (ImGui::BeginTabItem("Objects"))
			{
				DrawTable(snapshot->m_Objects, state.m_HideFreeSlots);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Materials"))
			{
				DrawTable(snapshot->m_Materials, state.m_HideFreeSlots);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Lights"))
			{
				DrawTable(snapshot->m_Lights, state.m_HideFreeSlots);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
