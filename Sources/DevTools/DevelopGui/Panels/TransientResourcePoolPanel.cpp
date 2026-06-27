#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/TransientResourcePoolPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/EnumText/EnumTextRenderGraph.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderGraph/RenderGraphSnapshot.h"
#include "Graphics/RenderGraph/RGTransientResourcePoolSnapshot.h"

namespace gglab
{
	namespace
	{
		struct TransientResourcePoolPanelState
		{
			bool m_HideDestroyed = true;
			char m_Filter[128] = {};
		};

		const char* SlotStateText(RGTransientPoolSlotState state) noexcept
		{
			switch (state)
			{
			case RGTransientPoolSlotState::Leased: return "Leased";
			case RGTransientPoolSlotState::PendingRetirement: return "PendingRetirement";
			case RGTransientPoolSlotState::Available: return "Available";
			case RGTransientPoolSlotState::Destroyed: return "Destroyed";
			}
			return "Unknown";
		}

		bool MatchesFilter(std::string_view text, const char* filter) noexcept
		{
			if (!filter || !*filter)
			{
				return true;
			}
			const std::string_view filterView(filter);
			return std::search(
				text.begin(), text.end(), filterView.begin(), filterView.end(),
				[](char lhs, char rhs)
				{
					return std::tolower(static_cast<unsigned char>(lhs)) ==
						std::tolower(static_cast<unsigned char>(rhs));
				}) != text.end();
		}

		std::string ClearValueText(const std::optional<RHIClearValue>& clearValue)
		{
			if (!clearValue)
			{
				return "None";
			}
			if (clearValue->m_IsDepthStencil)
			{
				return std::format("Depth={:.3f}, Stencil={}, Format={}",
					clearValue->m_Depth,
					clearValue->m_Stencil,
					static_cast<uint32_t>(clearValue->m_Format));
			}
			return std::format("Color=({:.3f}, {:.3f}, {:.3f}, {:.3f}), Format={}",
				clearValue->m_Color[0], clearValue->m_Color[1],
				clearValue->m_Color[2], clearValue->m_Color[3],
				static_cast<uint32_t>(clearValue->m_Format));
		}

		std::string FenceText(const RHIFencePoint& fence, bool completed)
		{
			if (!fence.IsValid())
			{
				return "-";
			}
			return std::format("{}:{} value={} ({})",
				fence.m_Fence.Index(), fence.m_Fence.Generation(), fence.m_Value,
				completed ? "complete" : "pending");
		}

		void DrawCounts(const char* label, const RGTransientPoolStateCounts& counts) noexcept
		{
			ImGui::Text("%s: %u total | %u leased | %u pending | %u available | %u destroyed",
				label, counts.m_Total, counts.m_Leased, counts.m_PendingRetirement,
				counts.m_Available, counts.m_Destroyed);
		}

		void DrawTextureTable(
			const RGTransientResourcePoolSnapshot& snapshot,
			const TransientResourcePoolPanelState& state) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("TransientTexturePool", 11, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Handle");
			ImGui::TableSetupColumn("Extent");
			ImGui::TableSetupColumn("Array");
			ImGui::TableSetupColumn("Mips");
			ImGui::TableSetupColumn("Samples");
			ImGui::TableSetupColumn("Format");
			ImGui::TableSetupColumn("Usage");
			ImGui::TableSetupColumn("Clear Value");
			ImGui::TableSetupColumn("Retirement Fence");
			ImGui::TableHeadersRow();

			for (const auto& texture : snapshot.m_Textures)
			{
				if (state.m_HideDestroyed && texture.m_State == RGTransientPoolSlotState::Destroyed)
				{
					continue;
				}
				const std::string searchable = std::format("{} {} {}x{}x{}",
					texture.m_PoolSlot.Value(), SlotStateText(texture.m_State),
					texture.m_Key.m_Extent.m_Width, texture.m_Key.m_Extent.m_Height,
					texture.m_Key.m_Extent.m_Depth);
				if (!MatchesFilter(searchable, state.m_Filter))
				{
					continue;
				}

				const std::string usage = devtools::EnumFlagsText<RHITextureUsage>(
					static_cast<std::underlying_type_t<RHITextureUsage>>(texture.m_Key.m_Usage));
				const std::string clearValue = ClearValueText(texture.m_Key.m_ClearValue);
				const std::string fence = FenceText(
					texture.m_RetirementFence, texture.m_RetirementFenceCompleted);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::Text("%u", texture.m_PoolSlot.Value());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(SlotStateText(texture.m_State));
				ImGui::TableSetColumnIndex(2);
				if (texture.m_Texture.IsValid()) ImGui::Text("%u:%u", texture.m_Texture.Index(), texture.m_Texture.Generation());
				else ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(3); ImGui::Text("%ux%ux%u", texture.m_Key.m_Extent.m_Width,
					texture.m_Key.m_Extent.m_Height, texture.m_Key.m_Extent.m_Depth);
				ImGui::TableSetColumnIndex(4); ImGui::Text("%u", texture.m_Key.m_ArraySize);
				ImGui::TableSetColumnIndex(5); ImGui::Text("%u", texture.m_Key.m_MipLevels);
				ImGui::TableSetColumnIndex(6); ImGui::Text("%u", texture.m_Key.m_SampleCount);
				ImGui::TableSetColumnIndex(7); ImGui::Text("%u", static_cast<uint32_t>(texture.m_Key.m_Format));
				ImGui::TableSetColumnIndex(8); ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(9); ImGui::TextUnformatted(clearValue.c_str());
				ImGui::TableSetColumnIndex(10); ImGui::TextUnformatted(fence.c_str());
			}
			ImGui::EndTable();
		}

		void DrawBufferTable(
			const RGTransientResourcePoolSnapshot& snapshot,
			const TransientResourcePoolPanelState& state) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("TransientBufferPool", 7, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Handle");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Stride");
			ImGui::TableSetupColumn("Usage");
			ImGui::TableSetupColumn("Retirement Fence");
			ImGui::TableHeadersRow();

			for (const auto& buffer : snapshot.m_Buffers)
			{
				if (state.m_HideDestroyed && buffer.m_State == RGTransientPoolSlotState::Destroyed)
				{
					continue;
				}
				const std::string searchable = std::format("{} {} {}",
					buffer.m_PoolSlot.Value(), SlotStateText(buffer.m_State), buffer.m_Key.m_SizeInBytes);
				if (!MatchesFilter(searchable, state.m_Filter))
				{
					continue;
				}

				const std::string usage = devtools::EnumFlagsText<RHIBufferUsage>(
					static_cast<std::underlying_type_t<RHIBufferUsage>>(buffer.m_Key.m_Usage));
				const std::string fence = FenceText(
					buffer.m_RetirementFence, buffer.m_RetirementFenceCompleted);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::Text("%u", buffer.m_PoolSlot.Value());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(SlotStateText(buffer.m_State));
				ImGui::TableSetColumnIndex(2);
				if (buffer.m_Buffer.IsValid()) ImGui::Text("%u:%u", buffer.m_Buffer.Index(), buffer.m_Buffer.Generation());
				else ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(3); ImGui::Text("%llu", static_cast<unsigned long long>(buffer.m_Key.m_SizeInBytes));
				ImGui::TableSetColumnIndex(4); ImGui::Text("%u", buffer.m_Key.m_StrideInBytes);
				ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(6); ImGui::TextUnformatted(fence.c_str());
			}
			ImGui::EndTable();
		}

		void DrawRenderGraphResources(RenderGraph* renderGraph) noexcept
		{
			if (!renderGraph)
			{
				ImGui::TextDisabled("RenderGraph is not available.");
				return;
			}
			RGSnapshot snapshot;
			BuildRenderGraphSnapshot(*renderGraph, snapshot);
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("TransientPoolRenderGraphResources", 8, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Resource");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Imported");
			ImGui::TableSetupColumn("Devirtualized");
			ImGui::TableSetupColumn("Pool Slot");
			ImGui::TableSetupColumn("Usage");
			ImGui::TableSetupColumn("First Pass");
			ImGui::TableSetupColumn("Last Pass");
			ImGui::TableHeadersRow();
			for (const auto& resource : snapshot.m_Resources)
			{
				const std::string usage = resource.m_ResourceType == RGResourceType::RGTexture ?
					devtools::EnumFlagsText<RHITextureUsage>(static_cast<uint32_t>(resource.m_UsageBits)) :
					devtools::EnumFlagsText<RHIBufferUsage>(static_cast<uint32_t>(resource.m_UsageBits));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(resource.m_Name.c_str());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(devtools::EnumText(resource.m_ResourceType).data());
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(resource.m_Imported ? "Yes" : "No");
				ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(resource.m_Devirtualized ? "Yes" : "No");
				ImGui::TableSetColumnIndex(4);
				if (resource.m_PoolSlot.IsValid()) ImGui::Text("%u", resource.m_PoolSlot.Value());
				else ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(5); ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(6); ImGui::Text("%d", resource.m_FirstUserPassIndex);
				ImGui::TableSetColumnIndex(7); ImGui::Text("%d", resource.m_LastUserPassIndex);
			}
			ImGui::EndTable();
		}
	}

	void TransientResourcePoolPanel::Draw(DevelopGuiContext& context) noexcept
	{
		auto& state = context.PanelState<TransientResourcePoolPanelState>();
		if (!context.m_Renderer || !context.m_Renderer->GetTransientResourcePool())
		{
			ImGui::TextDisabled("Transient resource pool is not available.");
			return;
		}

		RGTransientResourcePoolSnapshot snapshot;
		BuildRGTransientResourcePoolSnapshot(
			*context.m_Renderer->GetTransientResourcePool(), snapshot);

		DrawCounts("Textures", snapshot.m_TextureCounts);
		DrawCounts("Buffers", snapshot.m_BufferCounts);
		ImGui::Text("Pending retirements: %u | Free key buckets: %u texture / %u buffer | Max cached per key: %u",
			snapshot.m_PendingRetirementCount, snapshot.m_FreeTextureKeyCount,
			snapshot.m_FreeBufferKeyCount, snapshot.m_MaxCachedPerKey);
		ImGui::InputText("Filter", state.m_Filter, IM_ARRAYSIZE(state.m_Filter));
		ImGui::SameLine();
		ImGui::Checkbox("Hide Destroyed", &state.m_HideDestroyed);

		if (ImGui::BeginTabBar("TransientResourcePoolTabs"))
		{
			if (ImGui::BeginTabItem("Textures"))
			{
				DrawTextureTable(snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Buffers"))
			{
				DrawBufferTable(snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("RenderGraph Resources"))
			{
				DrawRenderGraphResources(context.m_RenderGraph);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
