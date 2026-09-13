#include "DevTools/DevelopGui/Panels/ResourceManagementPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiStyle.h"
#include "GGLabRuntime/Graphics/RHI/DX12/DX12ResourceLifecycleTools.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"

#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

namespace gglab
{
	namespace
	{
		const char* SlotStateText(DX12ResourceSnapshotState state) noexcept
		{
			switch (state)
			{
			case DX12ResourceSnapshotState::Free:
				return "Free";
			case DX12ResourceSnapshotState::Alive:
				return "Alive";
			case DX12ResourceSnapshotState::PendingRetirement:
				return "PendingRetirement";
			}
			return "Unknown";
		}

		const char* OwnershipText(DX12ResourceSnapshotOwnership ownership) noexcept
		{
			switch (ownership)
			{
			case DX12ResourceSnapshotOwnership::Owned:
				return "Owned";
			case DX12ResourceSnapshotOwnership::Borrowed:
				return "Borrowed";
			}
			return "Unknown";
		}

		std::string DebugIdentityText(const DX12ResourceSlotSnapshot& slot)
		{
			std::string result = std::format(
				"{}.{}", RHIResourceDebugDomainText(slot.m_DebugDomain), slot.m_DebugCategory);
			if (slot.m_HasDebugStableId)
			{
				result.append(std::format(" ID={}", slot.m_DebugStableId));
			}
			if (!slot.m_DebugLabel.empty())
			{
				result.append(" Name=");
				result.append(slot.m_DebugLabel);
			}
			if (!slot.m_DebugSource.empty())
			{
				result.append(" Source=");
				result.append(slot.m_DebugSource);
			}
			return result;
		}

		std::string DebugBindingText(const DX12ResourceSlotSnapshot& slot)
		{
			if (slot.m_DebugOwner.empty() && !slot.m_HasDebugBindingSerial)
			{
				return "-";
			}
			return std::format("{}{}{} | {} | history={}", slot.m_DebugOwner,
				slot.m_HasDebugBindingSerial ? " #" : "",
				slot.m_HasDebugBindingSerial ? std::to_string(slot.m_DebugBindingSerial) : "",
				slot.m_DebugBindingMode == RHIResourceDebugBindingMode::Exclusive ? "exclusive"
				: "aliased",
				slot.m_DebugBindingHistory.size());
		}

		void DrawDebugBindingHistoryTooltip(const DX12ResourceSlotSnapshot& slot) noexcept
		{
			if (slot.m_DebugBindingHistory.empty() || !ImGui::IsItemHovered())
			{
				return;
			}
			if (ImGui::BeginTooltip())
			{
				ImGui::TextUnformatted("Previous bindings");
				for (const auto& binding : slot.m_DebugBindingHistory)
				{
					ImGui::BulletText("%s%s%s | %s", binding.m_Owner.c_str(),
						binding.m_HasSerial ? " #" : "",
						binding.m_HasSerial ? std::to_string(binding.m_Serial).c_str() : "",
						binding.m_Mode == RHIResourceDebugBindingMode::Exclusive ? "exclusive"
						: "aliased");
				}
				ImGui::EndTooltip();
			}
		}

		const DX12ResourceSlotSnapshot* FindSlot(
			const DX12TestResourceSnapshot& entry, const DX12ResourceManagerSnapshot& snapshot) noexcept
		{
			if (entry.m_Type == DX12TestResourceType::Texture)
			{
				const uint32_t index = entry.m_Index;
				return index < snapshot.m_Textures.size() ? &snapshot.m_Textures[index] : nullptr;
			}

			const uint32_t index = entry.m_Index;
			return index < snapshot.m_Buffers.size() ? &snapshot.m_Buffers[index] : nullptr;
		}

		const char* EntryStatusText(const DX12TestResourceSnapshot& entry,
			const DX12ResourceManagerSnapshot& snapshot) noexcept
		{
			const bool alive = entry.m_Alive;
			if (alive)
			{
				return "Alive";
			}
			if (!entry.m_DestroyRequested)
			{
				return "Invalid";
			}

			const auto* slot = FindSlot(entry, snapshot);
			if (!slot)
			{
				return "Unknown";
			}
			if (slot->m_State == DX12ResourceSnapshotState::PendingRetirement)
			{
				return "PendingRetirement";
			}
			if (slot->m_State == DX12ResourceSnapshotState::Free)
			{
				return "Retired";
			}
			return "Slot Reused";
		}

		void DrawTestResourcesTable(DX12ResourceLifecycleControlBase* control,
			const DX12ResourceManagerSnapshot& snapshot,
			const DX12ResourceLifecycleSnapshot& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

			std::optional<uint64_t> removeId;
			if (ImGui::BeginTable(
				"ResourceManagementTestResources", 8, flags, ImVec2(0.0f, 260.0f)))
			{
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Generation");
				ImGui::TableSetupColumn("Status");
				ImGui::TableSetupColumn("Current Slot");
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Record");
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (size_t index = 0; index < state.m_Resources.size(); ++index)
				{
					const auto& entry = state.m_Resources[index];
					const auto* slot = FindSlot(entry, snapshot);

					ImGui::PushID(static_cast<int>(entry.m_Id));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(
						entry.m_Type == DX12TestResourceType::Texture ? "Texture" : "Buffer");
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(entry.m_Name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%u", entry.m_Index);
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", entry.m_Generation);
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(EntryStatusText(entry, snapshot));
					ImGui::TableSetColumnIndex(5);
					if (slot)
					{
						ImGui::Text(
							"%s / gen %u", SlotStateText(slot->m_State), slot->m_Generation);
					}
					else
					{
						ImGui::TextUnformatted("-");
					}
					ImGui::TableSetColumnIndex(6);
					ImGui::BeginDisabled(!control);
					if (!entry.m_DestroyRequested)
					{
						if (ImGui::SmallButton("Destroy") && control)
						{
							control->DestroyResource(entry.m_Id);
						}
					}
					else
					{
						ImGui::TextDisabled("Destroyed");
					}
					ImGui::TableSetColumnIndex(7);
					if (entry.m_DestroyRequested)
					{
						if (ImGui::SmallButton("Remove Row"))
						{
							removeId = entry.m_Id;
						}
					}
					else
					{
						ImGui::BeginDisabled();
						ImGui::SmallButton("Remove Row");
						ImGui::EndDisabled();
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			if (removeId && control)
			{
				control->RemoveDestroyedRow(*removeId);
			}
		}

		void DrawSlotsTable(
			const char* tableId, const std::vector<DX12ResourceSlotSnapshot>& slots) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable(tableId, 12, flags | ImGuiTableFlags_ScrollX))
			{
				return;
			}

			ImGui::TableSetupColumn("Index");
			ImGui::TableSetupColumn("Generation");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Ownership");
			ImGui::TableSetupColumn("Identity");
			ImGui::TableSetupColumn("Binding");
			ImGui::TableSetupColumn("Debug Name");
			ImGui::TableSetupColumn("Native");
			ImGui::TableSetupColumn("Last Use");
			ImGui::TableSetupColumn("Use Done");
			ImGui::TableSetupColumn("Retire");
			ImGui::TableSetupColumn("Retire Done");
			ImGui::TableHeadersRow();

			for (const auto& slot : slots)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", slot.m_Index);
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%u", slot.m_Generation);
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(SlotStateText(slot.m_State));
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(OwnershipText(slot.m_Ownership));
				ImGui::TableSetColumnIndex(4);
				const std::string identity = DebugIdentityText(slot);
				ImGui::TextUnformatted(identity.c_str());
				ImGui::TableSetColumnIndex(5);
				const std::string binding = DebugBindingText(slot);
				ImGui::TextUnformatted(binding.c_str());
				DrawDebugBindingHistoryTooltip(slot);
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(slot.m_DebugName.empty() ? "-" : slot.m_DebugName.c_str());
				ImGui::TableSetColumnIndex(7);
				ImGui::TextUnformatted(slot.m_NativeResourceValid ? "yes" : "no");
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%u", slot.m_LastUseFenceCount);
				ImGui::TableSetColumnIndex(9);
				ImGui::Text("%u", slot.m_CompletedLastUseFenceCount);
				ImGui::TableSetColumnIndex(10);
				ImGui::Text("%u", slot.m_PendingFenceCount);
				ImGui::TableSetColumnIndex(11);
				ImGui::Text("%u", slot.m_CompletedFenceCount);
			}

			ImGui::EndTable();
		}


	}

	void ResourceManagementPanel::Draw(DevelopGuiContext& context) noexcept
	{
		ImGui::TextUnformatted("RHI Resource Management");
		ImGui::Separator();

		const auto* view = context.m_DX12ResourceLifecycle;
		auto* control = context.m_DX12ResourceLifecycleControl;
		if (!view)
		{
			ImGui::TextColored(devtools::style::ErrorTextColor,
				"DX12 lifecycle mutation controls are disabled on this backend.");
			ImGui::TextWrapped(
				"Add/destroy test resources and Flush + Collect inject DX12 queue/fence operations "
				"that are not part of the backend-neutral RHI diagnostics contract. Vulkan memory, "
				"resource, descriptor, and retirement state remain available in Diagnostics/RHI/Vulkan Backend.");
			return;
		}

		ImGui::BeginDisabled(!control);
		if (ImGui::Button("Add Texture") && control)
		{
			control->AddTexture();
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Buffer") && control)
		{
			control->AddBuffer();
		}
		ImGui::SameLine();
		if (ImGui::Button("Destroy All") && control)
		{
			control->DestroyAll();
		}
		ImGui::SameLine();
		if (ImGui::Button("Signal + Record Use") && control)
		{
			control->SignalAndRecordUse();
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Destroyed Rows") && control)
		{
			control->ClearDestroyedRows();
		}

		ImGui::EndDisabled();
		auto state = view->GetSnapshot();

		const auto* snapshot =
			context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<DX12ResourceManagerSnapshot>()
			: nullptr;
		if (!snapshot)
		{
			ImGui::TextDisabled("DX12 resource snapshot provider is not available.");
			return;
		}
		DrawTestResourcesTable(control, *snapshot, state);

		ImGui::SeparatorText("Validation");
		ImGui::BeginDisabled(!control);
		if (ImGui::Button("Invalid Destroy Probe") && control)
		{
			control->ProbeInvalidDestroy();
		}
		ImGui::SameLine();
		if (ImGui::Button("Invalid Create Probe") && control)
		{
			control->ProbeInvalidCreate();
		}
		ImGui::SameLine();
		if (ImGui::Button("Collect Completed") && control)
		{
			control->CollectCompleted();
		}
		ImGui::SameLine();
		if (ImGui::Button("Flush + Collect") && control)
		{
			control->FlushAndCollect();
		}

		if (ImGui::Button("Run Lifecycle Test") && control)
		{
			control->RunLifecycleTest();
			state = view->GetSnapshot();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextUnformatted(state.m_LastTestResult.c_str());

		const auto& diagnostics = snapshot->m_Diagnostics;

		ImGui::SeparatorText("Diagnostics");
		ImGui::Text(
			"Created: textures=%llu buffers=%llu | Imported: textures=%llu buffers=%llu | Retired: textures=%llu buffers=%llu",
			static_cast<unsigned long long>(diagnostics.m_TextureCreateCount),
			static_cast<unsigned long long>(diagnostics.m_BufferCreateCount),
			static_cast<unsigned long long>(diagnostics.m_TextureImportCount),
			static_cast<unsigned long long>(diagnostics.m_BufferImportCount),
			static_cast<unsigned long long>(diagnostics.m_TextureRetireCount),
			static_cast<unsigned long long>(diagnostics.m_BufferRetireCount));
		ImGui::Text(
			"Validation: create failures=%llu import failures=%llu invalid uses=%llu invalid destroys=%llu stale destroys=%llu double destroys=%llu",
			static_cast<unsigned long long>(diagnostics.m_CreateFailureCount),
			static_cast<unsigned long long>(diagnostics.m_ImportFailureCount),
			static_cast<unsigned long long>(diagnostics.m_InvalidUseCount),
			static_cast<unsigned long long>(diagnostics.m_InvalidDestroyCount),
			static_cast<unsigned long long>(diagnostics.m_StaleDestroyCount),
			static_cast<unsigned long long>(diagnostics.m_DoubleDestroyCount));
		ImGui::Text("Naming: unspecified resource creates=%llu",
			static_cast<unsigned long long>(diagnostics.m_UnnamedResourceCreateCount));

		if (ImGui::BeginTabBar("ResourceManagementSlots"))
		{
			if (ImGui::BeginTabItem("Textures"))
			{
				DrawSlotsTable("RHITextureSlots", snapshot->m_Textures);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Buffers"))
			{
				DrawSlotsTable("RHIBufferSlots", snapshot->m_Buffers);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
