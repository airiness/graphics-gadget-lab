#include "Core/Precompiled.h"
#include "Graphics/DevelopGui/DevelopGuiRenderGraphInspectorPanel.h"
#include "Graphics/RenderGraph/RenderGraphInspector.h"

namespace gglab
{
	namespace
	{
		struct RenderGraphInspectorPanelState
		{
			bool m_HideCulledPasses = false;
			bool m_ShowBarrierDetails = true;
			int32_t m_SelectedPassIndex = -1;
			int32_t m_SelectedResourceIndex = -1;
			char m_Filter[128] = {};
		};

		static const char* ResourceTypeToString(RGResourceType type) noexcept
		{
			switch (type)
			{
			case RGResourceType::RGTexture:
				return "Texture";
			case RGResourceType::RGBuffer:
				return "Buffer";
			}

			return "Unknown";
		}

		static const char* AccessTypeToString(RGResourceAccessType type) noexcept
		{
			switch (type)
			{
			case RGResourceAccessType::Read:
				return "Read";
			case RGResourceAccessType::Write:
				return "Write";
			}

			return "Unknown";
		}

		static const char* BoolToString(bool value) noexcept
		{
			return value ? "Yes" : "No";
		}

		static bool TextMatchesFilter(const std::string& text, const char* filter) noexcept
		{
			if (!filter || !*filter)
			{
				return true;
			}

			return text.find(filter) != std::string::npos;
		}

		static void AppendUsageFlag(std::string& out, uint64_t bits, uint64_t flag, const char* name) noexcept
		{
			if ((bits & flag) == 0)
			{
				return;
			}

			if (!out.empty())
			{
				out += " | ";
			}
			out += name;
		}

		static std::string UsageBitsToString(uint64_t bits, RGResourceType resourceType) noexcept
		{
			std::string result;
			if (resourceType == RGResourceType::RGTexture)
			{
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::Sample), "Sample");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::RenderTarget), "RTV");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::DepthStencil), "DSV Write");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::DepthStencilRead), "DSV Read");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::UAV), "UAV");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::CopySrc), "CopySrc");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::CopyDst), "CopyDst");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGTextureUsage::Present), "Present");
			}
			else
			{
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::Vertex), "Vertex");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::Index), "Index");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::Constant), "Constant");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::UAV), "UAV");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::CopySrc), "CopySrc");
				AppendUsageFlag(result, bits, static_cast<uint64_t>(RGBufferUsage::CopyDst), "CopyDst");
			}

			return result.empty() ? "None" : result;
		}

		static std::string BarrierStateToString(const RGBarrierState& state) noexcept
		{
			return std::format("sync=0x{:X}, access=0x{:X}, layout={}",
				static_cast<uint64_t>(state.m_Sync),
				static_cast<uint64_t>(state.m_Access),
				static_cast<uint32_t>(state.m_Layout));
		}

		static std::string PassIndexToString(int32_t passIndex) noexcept
		{
			return passIndex >= 0 ? std::format("{}", passIndex) : "-";
		}

		static std::string ResourceIndexToString(ResourceIndex resourceIndex) noexcept
		{
			return resourceIndex.IsValid() ? std::format("{}", resourceIndex.Value()) : "-";
		}

		static std::string PassListToString(const std::vector<int32_t>& passIndices) noexcept
		{
			if (passIndices.empty())
			{
				return "-";
			}

			std::string result;
			for (const auto passIndex : passIndices)
			{
				if (!result.empty())
				{
					result += ", ";
				}
				result += PassIndexToString(passIndex);
			}
			return result;
		}

		static std::string ResourceListToString(
			const RGInspectorSnapshot& snapshot,
			const std::vector<uint32_t>& resourceIndices) noexcept
		{
			if (resourceIndices.empty())
			{
				return "-";
			}

			std::string result;
			for (const auto resourceIndex : resourceIndices)
			{
				if (!result.empty())
				{
					result += ", ";
				}

				if (resourceIndex < snapshot.m_Resources.size())
				{
					result += std::format("#{} {}", resourceIndex, snapshot.m_Resources[resourceIndex].m_Name);
				}
				else
				{
					result += "?";
				}
			}
			return result;
		}

		static bool PassMatchesFilter(const RGInspectorPassInfo& pass, const char* filter) noexcept
		{
			if (TextMatchesFilter(pass.m_Name, filter))
			{
				return true;
			}

			for (const auto& access : pass.m_Accesses)
			{
				if (TextMatchesFilter(access.m_ResourceName, filter))
				{
					return true;
				}
			}

			return false;
		}

		static bool ResourceMatchesFilter(const RGInspectorResourceInfo& resource, const char* filter) noexcept
		{
			return TextMatchesFilter(resource.m_Name, filter);
		}

		static void DrawBarrierTable(const char* label, const std::vector<RGInspectorBarrierInfo>& barriers) noexcept
		{
			if (barriers.empty())
			{
				ImGui::Text("%s: none", label);
				return;
			}

			ImGui::Text("%s: %u", label, static_cast<uint32_t>(barriers.size()));
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable(label, 4, flags))
			{
				return;
			}

			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Before");
			ImGui::TableSetupColumn("After");
			ImGui::TableHeadersRow();

			for (const auto& barrier : barriers)
			{
				const std::string before = BarrierStateToString(barrier.m_Before);
				const std::string after = BarrierStateToString(barrier.m_After);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(barrier.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(ResourceTypeToString(barrier.m_ResourceType));
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(before.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(after.c_str());
			}

			ImGui::EndTable();
		}

		static void DrawSelectedPassDetails(
			const RGInspectorSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept
		{
			if (state.m_SelectedPassIndex < 0 ||
				static_cast<size_t>(state.m_SelectedPassIndex) >= snapshot.m_Passes.size())
			{
				ImGui::TextDisabled("Select a pass to inspect its declared accesses and generated barriers.");
				return;
			}

			const auto& pass = snapshot.m_Passes[state.m_SelectedPassIndex];
			ImGui::SeparatorText("Selected Pass");
			ImGui::Text("Pass #%u: %s", pass.m_Index, pass.m_Name.c_str());
			ImGui::Text("Execution Order: %s", PassIndexToString(pass.m_ExecutionOrder).c_str());
			ImGui::Text("Culled: %s, SideEffect: %s", BoolToString(pass.m_Culled), BoolToString(pass.m_SideEffect));

			const std::string devirtualizeResources = ResourceListToString(snapshot, pass.m_DevirtualizeResources);
			const std::string destroyResources = ResourceListToString(snapshot, pass.m_DestroyResources);
			ImGui::TextWrapped("Devirtualize: %s", devirtualizeResources.c_str());
			ImGui::TextWrapped("Destroy: %s", destroyResources.c_str());

			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("SelectedPassAccesses", 6, flags))
			{
				ImGui::TableSetupColumn("Resource");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Access");
				ImGui::TableSetupColumn("Usage");
				ImGui::TableSetupColumn("Node");
				ImGui::TableSetupColumn("Version");
				ImGui::TableHeadersRow();

				for (const auto& access : pass.m_Accesses)
				{
					const std::string usage = UsageBitsToString(access.m_UsageBits, access.m_ResourceType);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(access.m_ResourceName.c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(ResourceTypeToString(access.m_ResourceType));
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(AccessTypeToString(access.m_AccessType));
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(usage.c_str());
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%u", access.m_ResourceNodeIndex);
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", access.m_ResourceVersion);
				}

				ImGui::EndTable();
			}

			if (state.m_ShowBarrierDetails)
			{
				DrawBarrierTable("Pre Barriers", pass.m_PreBarriers);
				DrawBarrierTable("Post Barriers", pass.m_PostBarriers);
			}
		}

		static void DrawSelectedResourceDetails(
			const RGInspectorSnapshot& snapshot,
			const RenderGraphInspectorPanelState& state) noexcept
		{
			if (state.m_SelectedResourceIndex < 0 ||
				static_cast<size_t>(state.m_SelectedResourceIndex) >= snapshot.m_Resources.size())
			{
				ImGui::TextDisabled("Select a resource to inspect lifetime and state.");
				return;
			}

			const auto& resource = snapshot.m_Resources[state.m_SelectedResourceIndex];
			const std::string initialState = BarrierStateToString(resource.m_InitialBarrierState);
			const std::string finalState = resource.m_HasFinalBarrierState ?
				BarrierStateToString(resource.m_FinalBarrierState) :
				"-";
			const std::string usage = UsageBitsToString(resource.m_UsageBits, resource.m_ResourceType);
			const std::string gpuIndex = ResourceIndexToString(resource.m_GpuResourceIndex);

			ImGui::SeparatorText("Selected Resource");
			ImGui::Text("Resource #%u: %s", resource.m_Index, resource.m_Name.c_str());
			ImGui::Text("Type: %s", ResourceTypeToString(resource.m_ResourceType));
			ImGui::Text("Imported: %s, Devirtualized: %s, RefCount: %d",
				BoolToString(resource.m_Imported),
				BoolToString(resource.m_Devirtualized),
				resource.m_RefCount);
			ImGui::Text("First User: %s, Last User: %s, GPU Index: %s",
				PassIndexToString(resource.m_FirstUserPassIndex).c_str(),
				PassIndexToString(resource.m_LastUserPassIndex).c_str(),
				gpuIndex.c_str());
			ImGui::TextWrapped("Accumulated Usage: %s", usage.c_str());
			ImGui::TextWrapped("Initial State: %s", initialState.c_str());
			ImGui::TextWrapped("Exported Final State: %s", finalState.c_str());
		}

		static void DrawPassesTab(
			const RGInspectorSnapshot& snapshot,
			RenderGraphInspectorPanelState& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("RenderGraphPasses", 8, flags, ImVec2(0.0f, 280.0f)))
			{
				ImGui::TableSetupColumn("Order");
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Culled");
				ImGui::TableSetupColumn("SideEffect");
				ImGui::TableSetupColumn("Accesses");
				ImGui::TableSetupColumn("Pre");
				ImGui::TableSetupColumn("Post");
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (const auto& pass : snapshot.m_Passes)
				{
					if (state.m_HideCulledPasses && pass.m_Culled)
					{
						continue;
					}
					if (!PassMatchesFilter(pass, state.m_Filter))
					{
						continue;
					}

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(PassIndexToString(pass.m_ExecutionOrder).c_str());
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%u", pass.m_Index);
					ImGui::TableSetColumnIndex(2);
					const bool selected = state.m_SelectedPassIndex == static_cast<int32_t>(pass.m_Index);
					const std::string label = std::format("{}##rg_pass_{}", pass.m_Name, pass.m_Index);
					if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						state.m_SelectedPassIndex = static_cast<int32_t>(pass.m_Index);
					}
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(BoolToString(pass.m_Culled));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(BoolToString(pass.m_SideEffect));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_Accesses.size()));
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_PreBarriers.size()));
					ImGui::TableSetColumnIndex(7);
					ImGui::Text("%u", static_cast<uint32_t>(pass.m_PostBarriers.size()));
				}

				ImGui::EndTable();
			}

			DrawSelectedPassDetails(snapshot, state);
		}

		static void DrawResourcesTab(
			const RGInspectorSnapshot& snapshot,
			RenderGraphInspectorPanelState& state) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (ImGui::BeginTable("RenderGraphResources", 10, flags, ImVec2(0.0f, 280.0f)))
			{
				ImGui::TableSetupColumn("Index");
				ImGui::TableSetupColumn("Name");
				ImGui::TableSetupColumn("Type");
				ImGui::TableSetupColumn("Imported");
				ImGui::TableSetupColumn("Devirt");
				ImGui::TableSetupColumn("Ref");
				ImGui::TableSetupColumn("First");
				ImGui::TableSetupColumn("Last");
				ImGui::TableSetupColumn("GPU");
				ImGui::TableSetupColumn("Usage");
				ImGui::TableSetupScrollFreeze(0, 1);
				ImGui::TableHeadersRow();

				for (const auto& resource : snapshot.m_Resources)
				{
					if (!ResourceMatchesFilter(resource, state.m_Filter))
					{
						continue;
					}

					const std::string usage = UsageBitsToString(resource.m_UsageBits, resource.m_ResourceType);
					const std::string gpuIndex = ResourceIndexToString(resource.m_GpuResourceIndex);

					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", resource.m_Index);
					ImGui::TableSetColumnIndex(1);
					const bool selected = state.m_SelectedResourceIndex == static_cast<int32_t>(resource.m_Index);
					const std::string label = std::format("{}##rg_resource_{}", resource.m_Name, resource.m_Index);
					if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						state.m_SelectedResourceIndex = static_cast<int32_t>(resource.m_Index);
					}
					ImGui::TableSetColumnIndex(2);
					ImGui::TextUnformatted(ResourceTypeToString(resource.m_ResourceType));
					ImGui::TableSetColumnIndex(3);
					ImGui::TextUnformatted(BoolToString(resource.m_Imported));
					ImGui::TableSetColumnIndex(4);
					ImGui::TextUnformatted(BoolToString(resource.m_Devirtualized));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%d", resource.m_RefCount);
					ImGui::TableSetColumnIndex(6);
					ImGui::TextUnformatted(PassIndexToString(resource.m_FirstUserPassIndex).c_str());
					ImGui::TableSetColumnIndex(7);
					ImGui::TextUnformatted(PassIndexToString(resource.m_LastUserPassIndex).c_str());
					ImGui::TableSetColumnIndex(8);
					ImGui::TextUnformatted(gpuIndex.c_str());
					ImGui::TableSetColumnIndex(9);
					ImGui::TextUnformatted(usage.c_str());
				}

				ImGui::EndTable();
			}

			DrawSelectedResourceDetails(snapshot, state);
		}

		static void DrawResourceNodesTab(const RGInspectorSnapshot& snapshot) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("RenderGraphResourceNodes", 7, flags, ImVec2(0.0f, 360.0f)))
			{
				return;
			}

			ImGui::TableSetupColumn("Node");
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("Version");
			ImGui::TableSetupColumn("Writer");
			ImGui::TableSetupColumn("Readers");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const auto& node : snapshot.m_ResourceNodes)
			{
				const std::string readers = PassListToString(node.m_ReaderPassIndices);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", node.m_Index);
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(node.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(ResourceTypeToString(node.m_ResourceType));
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", node.m_ResourceSlot);
				ImGui::TableSetColumnIndex(4);
				ImGui::Text("%u", node.m_ResourceVersion);
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(PassIndexToString(node.m_WriterPassIndex).c_str());
				ImGui::TableSetColumnIndex(6);
				ImGui::TextUnformatted(readers.c_str());
			}

			ImGui::EndTable();
		}

		static void DrawDependenciesTab(const RGInspectorSnapshot& snapshot) noexcept
		{
			const ImGuiTableFlags flags =
				ImGuiTableFlags_Borders |
				ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable |
				ImGuiTableFlags_ScrollY |
				ImGuiTableFlags_SizingStretchProp;

			if (!ImGui::BeginTable("RenderGraphDependencies", 5, flags, ImVec2(0.0f, 360.0f)))
			{
				return;
			}

			ImGui::TableSetupColumn("From");
			ImGui::TableSetupColumn("To");
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Node");
			ImGui::TableSetupColumn("Reason");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const auto& edge : snapshot.m_DependencyEdges)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%s #%s", edge.m_FromPassName.c_str(), PassIndexToString(edge.m_FromPassIndex).c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::Text("%s #%s", edge.m_ToPassName.c_str(), PassIndexToString(edge.m_ToPassIndex).c_str());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(edge.m_ResourceName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%u", edge.m_ResourceNodeIndex);
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted("Writer -> Reader");
			}

			ImGui::EndTable();
		}
	}

	void DevelopGuiRenderGraphInspectorPanel(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<RenderGraphInspectorPanelState>();

		ImGui::TextUnformatted("RenderGraph Inspector");
		ImGui::Separator();

		if (!context.m_RenderGraph)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "RenderGraph is null.");
			return;
		}

		RGInspectorSnapshot snapshot;
		BuildRenderGraphInspectorSnapshot(*context.m_RenderGraph, snapshot);

		uint32_t culledPassCount = 0;
		uint32_t preBarrierCount = 0;
		uint32_t postBarrierCount = 0;
		for (const auto& pass : snapshot.m_Passes)
		{
			culledPassCount += pass.m_Culled ? 1u : 0u;
			preBarrierCount += static_cast<uint32_t>(pass.m_PreBarriers.size());
			postBarrierCount += static_cast<uint32_t>(pass.m_PostBarriers.size());
		}

		ImGui::Text("Passes: %u live / %u total, Resources: %u, Nodes: %u, Edges: %u",
			static_cast<uint32_t>(snapshot.m_Passes.size()) - culledPassCount,
			static_cast<uint32_t>(snapshot.m_Passes.size()),
			static_cast<uint32_t>(snapshot.m_Resources.size()),
			static_cast<uint32_t>(snapshot.m_ResourceNodes.size()),
			static_cast<uint32_t>(snapshot.m_DependencyEdges.size()));
		ImGui::Text("Barriers: %u pre / %u post", preBarrierCount, postBarrierCount);

		ImGui::InputText("Filter", state.m_Filter, IM_ARRAYSIZE(state.m_Filter));
		ImGui::SameLine();
		ImGui::Checkbox("Hide Culled Passes", &state.m_HideCulledPasses);
		ImGui::SameLine();
		ImGui::Checkbox("Show Barrier Details", &state.m_ShowBarrierDetails);

		if (ImGui::BeginTabBar("RenderGraphInspectorTabs"))
		{
			if (ImGui::BeginTabItem("Passes"))
			{
				DrawPassesTab(snapshot, state);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Resources"))
			{
				DrawResourcesTab(snapshot, state);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Resource Nodes"))
			{
				DrawResourceNodesTab(snapshot);
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Dependencies"))
			{
				DrawDependenciesTab(snapshot);
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
}
