#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/ResourceManagementPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12ResourceManager.h"
#include "Graphics/RHI/DX12/DX12ResourceManagerSnapshot.h"

namespace gglab
{
	namespace
	{
		enum class TestResourceType : uint8_t
		{
			Texture,
			Buffer,
		};

		struct TestResourceEntry
		{
			uint64_t m_Id = 0;
			TestResourceType m_Type = TestResourceType::Texture;
			std::string m_Name;
			RHITextureHandle m_Texture{};
			RHIBufferHandle m_Buffer{};
			bool m_DestroyRequested = false;
		};

		struct ResourceManagementPanelState
		{
			std::vector<TestResourceEntry> m_Resources;
			std::string m_LastTestResult = "No lifecycle test has run.";
			uint32_t m_TextureSerial = 0;
			uint32_t m_BufferSerial = 0;
			uint64_t m_NextEntryId = 1;
		};

		const char* SlotStateText(DX12ResourceSnapshotState state) noexcept
		{
			switch (state)
			{
			case DX12ResourceSnapshotState::Free: return "Free";
			case DX12ResourceSnapshotState::Alive: return "Alive";
			case DX12ResourceSnapshotState::PendingRetirement: return "PendingRetirement";
			}
			return "Unknown";
		}

		const char* OwnershipText(DX12ResourceSnapshotOwnership ownership) noexcept
		{
			switch (ownership)
			{
			case DX12ResourceSnapshotOwnership::Owned: return "Owned";
			case DX12ResourceSnapshotOwnership::Borrowed: return "Borrowed";
			}
			return "Unknown";
		}

		RHITextureHandle CreateTestTexture(
			DX12Device& device,
			uint32_t serial,
			std::string* outName = nullptr) noexcept
		{
			const std::string name = std::format("ResourceManagement.TestTexture.{}", serial);
			if (outName)
			{
				*outName = name;
			}
			RHITextureDesc desc{};
			desc.m_Format = RHIFormat::R8G8B8A8Unorm;
			desc.m_Usage = RHITextureUsage::Sampled | RHITextureUsage::CopyDest;
			desc.m_Extent = { 16, 16, 1 };
			desc.m_DebugName = name.c_str();
			return device.CreateTexture(desc);
		}

		RHIBufferHandle CreateTestBuffer(
			DX12Device& device,
			uint32_t serial,
			std::string* outName = nullptr) noexcept
		{
			const std::string name = std::format("ResourceManagement.TestBuffer.{}", serial);
			if (outName)
			{
				*outName = name;
			}
			RHIBufferDesc desc{};
			desc.m_SizeInBytes = 4096;
			desc.m_StrideInBytes = 16;
			desc.m_Usage = RHIBufferUsage::Structured | RHIBufferUsage::CopyDest;
			desc.m_DebugName = name.c_str();
			return device.CreateBuffer(desc);
		}

		void AddTestTexture(DX12Device& device, ResourceManagementPanelState& state) noexcept
		{
			TestResourceEntry entry{};
			entry.m_Id = state.m_NextEntryId++;
			entry.m_Type = TestResourceType::Texture;
			entry.m_Texture = CreateTestTexture(device, ++state.m_TextureSerial, &entry.m_Name);
			if (entry.m_Texture.IsValid())
			{
				state.m_Resources.push_back(std::move(entry));
			}
		}

		void AddTestBuffer(DX12Device& device, ResourceManagementPanelState& state) noexcept
		{
			TestResourceEntry entry{};
			entry.m_Id = state.m_NextEntryId++;
			entry.m_Type = TestResourceType::Buffer;
			entry.m_Buffer = CreateTestBuffer(device, ++state.m_BufferSerial, &entry.m_Name);
			if (entry.m_Buffer.IsValid())
			{
				state.m_Resources.push_back(std::move(entry));
			}
		}

		void DestroyTestResource(DX12Device& device, TestResourceEntry& entry) noexcept
		{
			if (entry.m_DestroyRequested)
			{
				return;
			}

			if (entry.m_Type == TestResourceType::Texture)
			{
				device.DestroyTexture(entry.m_Texture);
			}
			else
			{
				device.DestroyBuffer(entry.m_Buffer);
			}
			entry.m_DestroyRequested = true;
		}

		const DX12ResourceSlotSnapshot* FindSlot(
			const TestResourceEntry& entry,
			const DX12ResourceManagerSnapshot& snapshot) noexcept
		{
			if (entry.m_Type == TestResourceType::Texture)
			{
				const uint32_t index = entry.m_Texture.Index();
				return index < snapshot.m_Textures.size() ? &snapshot.m_Textures[index] : nullptr;
			}

			const uint32_t index = entry.m_Buffer.Index();
			return index < snapshot.m_Buffers.size() ? &snapshot.m_Buffers[index] : nullptr;
		}

		const char* EntryStatusText(
			const TestResourceEntry& entry,
			const DX12Device& device,
			const DX12ResourceManagerSnapshot& snapshot) noexcept
		{
			const bool alive = entry.m_Type == TestResourceType::Texture ?
				device.IsAlive(entry.m_Texture) :
				device.IsAlive(entry.m_Buffer);
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

		uint32_t EntryIndex(const TestResourceEntry& entry) noexcept
		{
			return entry.m_Type == TestResourceType::Texture ?
				entry.m_Texture.Index() :
				entry.m_Buffer.Index();
		}

		uint32_t EntryGeneration(const TestResourceEntry& entry) noexcept
		{
			return entry.m_Type == TestResourceType::Texture ?
				entry.m_Texture.Generation() :
				entry.m_Buffer.Generation();
		}

		void DrawTestResourcesTable(
			DX12Device& device,
			const DX12ResourceManagerSnapshot& snapshot,
			ResourceManagementPanelState& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp |
				ImGuiTableFlags_ScrollY;

			std::optional<size_t> removeIndex;
			if (ImGui::BeginTable("ResourceManagementTestResources", 8, flags, ImVec2(0.0f, 260.0f)))
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
					auto& entry = state.m_Resources[index];
					const auto* slot = FindSlot(entry, snapshot);

					ImGui::PushID(static_cast<int>(entry.m_Id));
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(
						entry.m_Type == TestResourceType::Texture ? "Texture" : "Buffer");
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(entry.m_Name.c_str());
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%u", EntryIndex(entry));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%u", EntryGeneration(entry));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(EntryStatusText(entry, device, snapshot));
					ImGui::TableSetColumnIndex(5);
					if (slot)
					{
						ImGui::Text(
							"%s / gen %u",
							SlotStateText(slot->m_State),
							slot->m_Generation);
					}
					else
					{
						ImGui::TextUnformatted("-");
					}
					ImGui::TableSetColumnIndex(6);
					if (!entry.m_DestroyRequested)
					{
						if (ImGui::SmallButton("Destroy"))
						{
							DestroyTestResource(device, entry);
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
							removeIndex = index;
						}
					}
					else
					{
						ImGui::BeginDisabled();
						ImGui::SmallButton("Remove Row");
						ImGui::EndDisabled();
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			if (removeIndex.has_value())
			{
				state.m_Resources.erase(state.m_Resources.begin() + removeIndex.value());
			}
		}

		void DrawSlotsTable(
			const char* tableId,
			const std::vector<DX12ResourceSlotSnapshot>& slots) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable(tableId, 8, flags))
			{
				return;
			}

			ImGui::TableSetupColumn("Index");
			ImGui::TableSetupColumn("Generation");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Ownership");
			ImGui::TableSetupColumn("Debug Name");
			ImGui::TableSetupColumn("Native");
			ImGui::TableSetupColumn("Fences");
			ImGui::TableSetupColumn("Completed");
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
				ImGui::TextUnformatted(slot.m_DebugName.empty() ? "-" : slot.m_DebugName.c_str());
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(slot.m_NativeResourceValid ? "yes" : "no");
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%u", slot.m_PendingFenceCount);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%u", slot.m_CompletedFenceCount);
			}

			ImGui::EndTable();
		}

		void RunLifecycleTest(
			DX12Device& device,
			DX12ResourceManager& manager,
			ResourceManagementPanelState& state) noexcept
		{
			const RHITextureHandle first = CreateTestTexture(device, ++state.m_TextureSerial);
			const bool created = first.IsValid() && device.IsAlive(first);
			device.DestroyTexture(first);
			const bool invalidatedImmediately = !device.IsAlive(first);

			device.FlushGPU();
			manager.RetireCompletedResources();

			const RHITextureHandle replacement = CreateTestTexture(device, ++state.m_TextureSerial);
			const bool reusedSlot = replacement.IsValid() && replacement.Index() == first.Index();
			const bool generationChanged =
				replacement.IsValid() && replacement.Generation() != first.Generation();

			device.DestroyTexture(first);
			device.DestroyTexture(replacement);
			device.FlushGPU();
			manager.RetireCompletedResources();

			auto wrappedGeneration =
				std::numeric_limits<RHITextureHandle::GenerationType>::max();
			++wrappedGeneration;
			if (wrappedGeneration == RHITextureHandle::InvalidGeneration)
			{
				++wrappedGeneration;
			}
			const bool rolloverValid =
				wrappedGeneration != RHITextureHandle::InvalidGeneration;
			const bool passed =
				created &&
				invalidatedImmediately &&
				reusedSlot &&
				generationChanged &&
				rolloverValid;

			state.m_LastTestResult = std::format(
				"{} | create={} immediate-invalidate={} slot-reuse={} generation-change={} rollover={}",
				passed ? "PASS" : "FAIL",
				created,
				invalidatedImmediately,
				reusedSlot,
				generationChanged,
				rolloverValid);
		}
	}

	void ResourceManagementPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<ResourceManagementPanelState>();

		ImGui::TextUnformatted("RHI Resource Management Lab");
		ImGui::Separator();

		if (!context.m_Renderer || !context.m_Renderer->GetDevice())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "DX12Device is unavailable.");
			return;
		}

		auto& device = *context.m_Renderer->GetDevice();
		auto& manager = *device.GetResourceManager();

		if (ImGui::Button("Add Texture"))
		{
			AddTestTexture(device, state);
		}
		ImGui::SameLine();
		if (ImGui::Button("Add Buffer"))
		{
			AddTestBuffer(device, state);
		}
		ImGui::SameLine();
		if (ImGui::Button("Destroy All"))
		{
			for (auto& entry : state.m_Resources)
			{
				DestroyTestResource(device, entry);
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Clear Destroyed Rows"))
		{
			std::erase_if(
				state.m_Resources,
				[](const TestResourceEntry& entry)
				{
					return entry.m_DestroyRequested;
				});
		}

		DX12ResourceManagerSnapshot snapshot;
		BuildDX12ResourceManagerSnapshot(manager, snapshot);
		DrawTestResourcesTable(device, snapshot, state);

		ImGui::SeparatorText("Validation");
		if (ImGui::Button("Invalid Destroy Probe"))
		{
			device.DestroyTexture({});
			device.DestroyBuffer({});
		}
		ImGui::SameLine();
		if (ImGui::Button("Invalid Create Probe"))
		{
			RHITextureDesc invalidTexture{};
			RHIBufferDesc invalidBuffer{};
			GGLAB_UNUSED(device.CreateTexture(invalidTexture));
			GGLAB_UNUSED(device.CreateBuffer(invalidBuffer));
		}
		ImGui::SameLine();
		if (ImGui::Button("Collect Completed"))
		{
			manager.RetireCompletedResources();
		}
		ImGui::SameLine();
		if (ImGui::Button("Flush + Collect"))
		{
			device.FlushGPU();
			manager.RetireCompletedResources();
		}

		if (ImGui::Button("Run Lifecycle Test"))
		{
			RunLifecycleTest(device, manager, state);
		}
		ImGui::SameLine();
		ImGui::TextUnformatted(state.m_LastTestResult.c_str());

		const auto& diagnostics = snapshot.m_Diagnostics;

		ImGui::SeparatorText("Diagnostics");
		ImGui::Text(
			"Created: textures=%llu buffers=%llu | Retired: textures=%llu buffers=%llu",
			static_cast<unsigned long long>(diagnostics.m_TextureCreateCount),
			static_cast<unsigned long long>(diagnostics.m_BufferCreateCount),
			static_cast<unsigned long long>(diagnostics.m_TextureRetireCount),
			static_cast<unsigned long long>(diagnostics.m_BufferRetireCount));
		ImGui::Text(
			"Validation: create failures=%llu invalid destroys=%llu stale destroys=%llu double destroys=%llu",
			static_cast<unsigned long long>(diagnostics.m_CreateFailureCount),
			static_cast<unsigned long long>(diagnostics.m_InvalidDestroyCount),
			static_cast<unsigned long long>(diagnostics.m_StaleDestroyCount),
			static_cast<unsigned long long>(diagnostics.m_DoubleDestroyCount));

		if (ImGui::BeginTabBar("ResourceManagementSlots"))
		{
			if (ImGui::BeginTabItem("Textures"))
			{
				DrawSlotsTable("RHITextureSlots", snapshot.m_Textures);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Buffers"))
			{
				DrawSlotsTable("RHIBufferSlots", snapshot.m_Buffers);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
