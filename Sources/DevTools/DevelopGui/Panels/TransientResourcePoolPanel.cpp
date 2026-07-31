#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/Panels/TransientResourcePoolPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/EnumText/EnumTextRenderGraph.h"
#include "DevTools/RHIText.h"
#include "Core/Utility/StringUtils.h"
#include "Graphics/Renderer.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "Diagnostics/Snapshots/RenderGraphSnapshot.h"
#include "Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"

namespace gglab
{
	namespace
	{
		struct TransientResourcePoolPanelState
		{
			bool m_HideDestroyed = true;
			char m_Filter[128] = {};
		};

		const char* SlotStateText(TransientPoolSlotState state) noexcept
		{
			switch (state)
			{
			case TransientPoolSlotState::Leased:
				return "Leased";
			case TransientPoolSlotState::PendingRetirement:
				return "PendingRetirement";
			case TransientPoolSlotState::Available:
				return "Available";
			case TransientPoolSlotState::Destroyed:
				return "Destroyed";
			}
			return "Unknown";
		}

		std::string ClearValueText(const std::optional<RHIClearValue>& clearValue)
		{
			if (!clearValue)
			{
				return "None";
			}
			if (clearValue->m_IsDepthStencil)
			{
				return std::format("Depth={:.3f}, Stencil={}, Format={}", clearValue->m_Depth,
					clearValue->m_Stencil, static_cast<uint32_t>(clearValue->m_Format));
			}
			return std::format("Color=({:.3f}, {:.3f}, {:.3f}, {:.3f}), Format={}",
				clearValue->m_Color[0], clearValue->m_Color[1], clearValue->m_Color[2],
				clearValue->m_Color[3], static_cast<uint32_t>(clearValue->m_Format));
		}

		std::string FenceText(const RHIFencePoint& fence, bool completed)
		{
			if (!fence.IsValid())
			{
				return "-";
			}
			return std::format("{} value={} ({})", devtools::RHIHandleText(fence.m_Fence),
				fence.m_Value, completed ? "complete" : "pending");
		}

		void DrawCounts(const char* label, const TransientPoolStateCounts& counts) noexcept
		{
			ImGui::Text("%s: %u total | %u leased | %u pending | %u available | %u destroyed",
				label, counts.m_Total, counts.m_Leased, counts.m_PendingRetirement,
				counts.m_Available, counts.m_Destroyed);
		}

		void DrawTextureTable(const TransientResourcePoolSnapshot& snapshot,
			const TransientResourcePoolPanelState& state) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
				ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("TransientTexturePool", 14, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Logical Owner");
			ImGui::TableSetupColumn("Acquire #");
			ImGui::TableSetupColumn("Native Debug Name");
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
				if (state.m_HideDestroyed && texture.m_State == TransientPoolSlotState::Destroyed)
				{
					continue;
				}
				const std::string searchable = std::format("{} {} {} {} {}x{}x{}",
					texture.m_PoolSlot.Value(), SlotStateText(texture.m_State),
					texture.m_LogicalName, texture.m_DebugName, texture.m_Key.m_Extent.m_Width,
					texture.m_Key.m_Extent.m_Height, texture.m_Key.m_Extent.m_Depth);
				if (!utils::ContainsIgnoreCase(searchable, state.m_Filter))
				{
					continue;
				}

				const std::string usage = devtools::EnumFlagsText<RHITextureUsage>(
					static_cast<std::underlying_type_t<RHITextureUsage>>(texture.m_Key.m_Usage));
				const std::string clearValue = ClearValueText(texture.m_Key.m_ClearValue);
				const std::string fence =
					FenceText(texture.m_RetirementFence, texture.m_RetirementFenceCompleted);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", texture.m_PoolSlot.Value());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(SlotStateText(texture.m_State));
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(texture.m_LogicalName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%llu", static_cast<unsigned long long>(texture.m_AcquireSerial));
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(texture.m_DebugName.c_str());
				ImGui::TableSetColumnIndex(5);
				const std::string textureHandle = devtools::RHIHandleText(texture.m_Texture);
				ImGui::TextUnformatted(textureHandle.c_str());
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%ux%ux%u", texture.m_Key.m_Extent.m_Width,
					texture.m_Key.m_Extent.m_Height, texture.m_Key.m_Extent.m_Depth);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%u", texture.m_Key.m_ArraySize);
				ImGui::TableSetColumnIndex(8);
				ImGui::Text("%u", texture.m_Key.m_MipLevels);
				ImGui::TableSetColumnIndex(9);
				ImGui::Text("%u", texture.m_Key.m_SampleCount);
				ImGui::TableSetColumnIndex(10);
				ImGui::Text("%u", static_cast<uint32_t>(texture.m_Key.m_Format));
				ImGui::TableSetColumnIndex(11);
				ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(12);
				ImGui::TextUnformatted(clearValue.c_str());
				ImGui::TableSetColumnIndex(13);
				ImGui::TextUnformatted(fence.c_str());
			}
			ImGui::EndTable();
		}

		void DrawBufferTable(const TransientResourcePoolSnapshot& snapshot,
			const TransientResourcePoolPanelState& state) noexcept
		{
			const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
				ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
				ImGuiTableFlags_ScrollY;
			if (!ImGui::BeginTable("TransientBufferPool", 10, flags))
			{
				return;
			}
			ImGui::TableSetupColumn("Slot");
			ImGui::TableSetupColumn("State");
			ImGui::TableSetupColumn("Logical Owner");
			ImGui::TableSetupColumn("Acquire #");
			ImGui::TableSetupColumn("Native Debug Name");
			ImGui::TableSetupColumn("Handle");
			ImGui::TableSetupColumn("Size");
			ImGui::TableSetupColumn("Stride");
			ImGui::TableSetupColumn("Usage");
			ImGui::TableSetupColumn("Retirement Fence");
			ImGui::TableHeadersRow();

			for (const auto& buffer : snapshot.m_Buffers)
			{
				if (state.m_HideDestroyed && buffer.m_State == TransientPoolSlotState::Destroyed)
				{
					continue;
				}
				const std::string searchable = std::format("{} {} {} {} {}",
					buffer.m_PoolSlot.Value(), SlotStateText(buffer.m_State), buffer.m_LogicalName,
					buffer.m_DebugName, buffer.m_Key.m_SizeInBytes);
				if (!utils::ContainsIgnoreCase(searchable, state.m_Filter))
				{
					continue;
				}

				const std::string usage = devtools::EnumFlagsText<RHIBufferUsage>(
					static_cast<std::underlying_type_t<RHIBufferUsage>>(buffer.m_Key.m_Usage));
				const std::string fence =
					FenceText(buffer.m_RetirementFence, buffer.m_RetirementFenceCompleted);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("%u", buffer.m_PoolSlot.Value());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(SlotStateText(buffer.m_State));
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(buffer.m_LogicalName.c_str());
				ImGui::TableSetColumnIndex(3);
				ImGui::Text("%llu", static_cast<unsigned long long>(buffer.m_AcquireSerial));
				ImGui::TableSetColumnIndex(4);
				ImGui::TextUnformatted(buffer.m_DebugName.c_str());
				ImGui::TableSetColumnIndex(5);
				const std::string bufferHandle = devtools::RHIHandleText(buffer.m_Buffer);
				ImGui::TextUnformatted(bufferHandle.c_str());
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%llu", static_cast<unsigned long long>(buffer.m_Key.m_SizeInBytes));
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%u", buffer.m_Key.m_StrideInBytes);
				ImGui::TableSetColumnIndex(8);
				ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(9);
				ImGui::TextUnformatted(fence.c_str());
			}
			ImGui::EndTable();
		}

		void DrawRenderGraphResources(DiagnosticsRuntime* diagnostics) noexcept
		{
			const auto* snapshot = diagnostics ? diagnostics->GetSnapshot<RGSnapshot>() : nullptr;
			if (!snapshot)
			{
				ImGui::TextDisabled("RenderGraph snapshot is not available.");
				return;
			}
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
			for (const auto& resource : snapshot->m_Resources)
			{
				const std::string usage = resource.m_ResourceType == RGResourceType::RGTexture
					? devtools::EnumFlagsText<RHITextureUsage>(
						static_cast<uint32_t>(resource.m_UsageBits))
					: devtools::EnumFlagsText<RHIBufferUsage>(
						static_cast<uint32_t>(resource.m_UsageBits));
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(resource.m_Name.c_str());
				ImGui::TableSetColumnIndex(1);
				ImGui::TextUnformatted(devtools::EnumText(resource.m_ResourceType).data());
				ImGui::TableSetColumnIndex(2);
				ImGui::TextUnformatted(resource.m_Imported ? "Yes" : "No");
				ImGui::TableSetColumnIndex(3);
				ImGui::TextUnformatted(resource.m_Devirtualized ? "Yes" : "No");
				ImGui::TableSetColumnIndex(4);
				if (resource.m_PoolSlot.IsValid())
					ImGui::Text("%u", resource.m_PoolSlot.Value());
				else
					ImGui::TextUnformatted("-");
				ImGui::TableSetColumnIndex(5);
				ImGui::TextUnformatted(usage.c_str());
				ImGui::TableSetColumnIndex(6);
				ImGui::Text("%d", resource.m_FirstUserPassIndex);
				ImGui::TableSetColumnIndex(7);
				ImGui::Text("%d", resource.m_LastUserPassIndex);
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

		const auto* snapshot =
			context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<TransientResourcePoolSnapshot>()
			: nullptr;
		if (!snapshot)
		{
			ImGui::TextDisabled("Transient pool snapshot provider is not available.");
			return;
		}

		DrawCounts("Textures", snapshot->m_TextureCounts);
		DrawCounts("Buffers", snapshot->m_BufferCounts);
		ImGui::Text(
			"Pending retirements: %u | Free key buckets: %u texture / %u buffer | Max cached per key: %u",
			snapshot->m_PendingRetirementCount, snapshot->m_FreeTextureKeyCount,
			snapshot->m_FreeBufferKeyCount, snapshot->m_MaxCachedPerKey);
		ImGui::InputText("Filter", state.m_Filter, IM_ARRAYSIZE(state.m_Filter));
		ImGui::SameLine();
		ImGui::Checkbox("Hide Destroyed", &state.m_HideDestroyed);

		if (ImGui::BeginTabBar("TransientResourcePoolTabs"))
		{
			if (ImGui::BeginTabItem("Textures"))
			{
				DrawTextureTable(*snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Buffers"))
			{
				DrawBufferTable(*snapshot, state);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("RenderGraph Resources"))
			{
				DrawRenderGraphResources(context.m_Diagnostics);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
	}
}
