#include "DevTools/DevelopGui/Panels/DX12BackendSummaryPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "GGLabRuntime/Diagnostics/DiagnosticsView.h"
#include "GGLabRuntime/Diagnostics/Snapshots/DX12BackendSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/DX12ResourceManagerSnapshot.h"
#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"

#include <algorithm>
#include <d3d12.h>
#include <imgui.h>

namespace gglab
{
	namespace
	{
		const char* EnabledText(bool enabled) noexcept
		{
			return enabled ? "enabled" : "disabled";
		}

		double MiB(uint64_t bytes) noexcept
		{
			return static_cast<double>(bytes) / (1024.0 * 1024.0);
		}

		const char* FeatureLevelText(uint32_t featureLevel) noexcept
		{
			switch (static_cast<D3D_FEATURE_LEVEL>(featureLevel))
			{
			case D3D_FEATURE_LEVEL_12_2:
				return "12_2";
			case D3D_FEATURE_LEVEL_12_1:
				return "12_1";
			case D3D_FEATURE_LEVEL_12_0:
				return "12_0";
			default:
				return "unavailable";
			}
		}

		void DrawMemorySegment(
			const char* label, const DX12MemorySegmentSnapshot& memory) noexcept
		{
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(label);
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%.2f", MiB(memory.m_BudgetBytes));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.2f", MiB(memory.m_UsageBytes));
			ImGui::TableSetColumnIndex(3);
			ImGui::Text("%u", memory.m_BlockCount);
			ImGui::TableSetColumnIndex(4);
			ImGui::Text("%.2f", MiB(memory.m_BlockBytes));
			ImGui::TableSetColumnIndex(5);
			ImGui::Text("%u", memory.m_AllocationCount);
			ImGui::TableSetColumnIndex(6);
			ImGui::Text("%.2f", MiB(memory.m_AllocationBytes));
		}

		uint32_t CountResourceSlots(const std::vector<DX12ResourceSlotSnapshot>& slots,
			DX12ResourceSnapshotState state) noexcept
		{
			return static_cast<uint32_t>(std::ranges::count_if(slots,
				[state](const DX12ResourceSlotSnapshot& slot) { return slot.m_State == state; }));
		}

		void DrawResourceSummary(const DX12ResourceManagerSnapshot* resources) noexcept
		{
			ImGui::SeparatorText("Resource manager");
			if (!resources)
			{
				ImGui::TextDisabled("Resource diagnostics are unavailable.");
				return;
			}

			const uint32_t liveTextures =
				CountResourceSlots(resources->m_Textures, DX12ResourceSnapshotState::Alive);
			const uint32_t retiredTextures = CountResourceSlots(
				resources->m_Textures, DX12ResourceSnapshotState::PendingRetirement);
			const uint32_t liveBuffers =
				CountResourceSlots(resources->m_Buffers, DX12ResourceSnapshotState::Alive);
			const uint32_t retiredBuffers = CountResourceSlots(
				resources->m_Buffers, DX12ResourceSnapshotState::PendingRetirement);
			ImGui::Text("Textures %u live / %u retired | Buffers %u live / %u retired",
				liveTextures, retiredTextures, liveBuffers, retiredBuffers);
			const auto& diagnostics = resources->m_Diagnostics;
			ImGui::TextDisabled(
				"Creates %llu textures / %llu buffers | imports %llu / %llu | retires %llu / %llu",
				static_cast<unsigned long long>(diagnostics.m_TextureCreateCount),
				static_cast<unsigned long long>(diagnostics.m_BufferCreateCount),
				static_cast<unsigned long long>(diagnostics.m_TextureImportCount),
				static_cast<unsigned long long>(diagnostics.m_BufferImportCount),
				static_cast<unsigned long long>(diagnostics.m_TextureRetireCount),
				static_cast<unsigned long long>(diagnostics.m_BufferRetireCount));
			ImGui::TextDisabled(
				"Failures create=%llu import=%llu | invalid use=%llu destroy=%llu stale=%llu double=%llu",
				static_cast<unsigned long long>(diagnostics.m_CreateFailureCount),
				static_cast<unsigned long long>(diagnostics.m_ImportFailureCount),
				static_cast<unsigned long long>(diagnostics.m_InvalidUseCount),
				static_cast<unsigned long long>(diagnostics.m_InvalidDestroyCount),
				static_cast<unsigned long long>(diagnostics.m_StaleDestroyCount),
				static_cast<unsigned long long>(diagnostics.m_DoubleDestroyCount));
		}
	}

	void DX12BackendSummaryPanel::Draw(DevelopGuiContext& context) noexcept
	{
		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<DX12BackendSnapshot>()
			: nullptr;
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("The active RHI backend is not DirectX 12.");
			return;
		}

		ImGui::SeparatorText("Adapter");
		ImGui::TextUnformatted(snapshot->m_DeviceName.c_str());
		ImGui::Text("Direct3D feature level: %s", FeatureLevelText(snapshot->m_FeatureLevel));
		if (snapshot->m_HasDriverVersion)
		{
			ImGui::Text("Driver: %u.%u.%u.%u", snapshot->m_DriverProduct,
				snapshot->m_DriverVersion, snapshot->m_DriverSubVersion, snapshot->m_DriverBuild);
		}
		ImGui::Text("Vendor: 0x%04X | Device: 0x%04X | Subsystem: 0x%08X | Revision: %u",
			snapshot->m_VendorId, snapshot->m_DeviceId, snapshot->m_SubSystemId,
			snapshot->m_Revision);
		ImGui::Text("LUID: %08X:%08X", static_cast<uint32_t>(snapshot->m_AdapterLuidHigh),
			snapshot->m_AdapterLuidLow);
		ImGui::Text("Dedicated video %.2f MiB | shared system %.2f MiB",
			MiB(snapshot->m_DedicatedVideoMemory), MiB(snapshot->m_SharedSystemMemory));
		if (snapshot->m_DedicatedSystemMemory > 0)
		{
			ImGui::Text("Dedicated system memory: %.2f MiB",
				MiB(snapshot->m_DedicatedSystemMemory));
		}

		ImGui::SeparatorText("Capabilities");
		ImGui::Text("Ray tracing: %s | mesh shaders: %s | enhanced barriers: %s",
			EnabledText(snapshot->m_RayTracingSupported),
			EnabledText(snapshot->m_MeshShaderSupported),
			EnabledText(snapshot->m_EnhancedBarriersSupported));
		ImGui::Text("Tearing: %s | GPU profiler: %s",
			EnabledText(snapshot->m_TearingSupported),
			EnabledText(snapshot->m_GpuProfilerEnabled));
		if (snapshot->m_WaveOperationsSupported)
		{
			ImGui::Text("Wave operations: enabled, lane count %u-%u",
				snapshot->m_MinWaveLaneCount, snapshot->m_MaxWaveLaneCount);
		}
		else
		{
			ImGui::Text("Wave operations: disabled");
		}

		ImGui::SeparatorText("D3D12MA memory");
		ImGui::Text("Architecture: %s%s", snapshot->m_IsUma ? "UMA" : "discrete",
			snapshot->m_IsCacheCoherentUma ? ", cache coherent" : "");
		if (ImGui::BeginTable("DX12MemorySegments", 7,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("Segment");
			ImGui::TableSetupColumn("Budget MiB");
			ImGui::TableSetupColumn("Usage MiB");
			ImGui::TableSetupColumn("Blocks");
			ImGui::TableSetupColumn("Block MiB");
			ImGui::TableSetupColumn("Allocations");
			ImGui::TableSetupColumn("Alloc MiB");
			ImGui::TableHeadersRow();
			DrawMemorySegment("Local", snapshot->m_LocalMemory);
			DrawMemorySegment("Non-local", snapshot->m_NonLocalMemory);
			ImGui::EndTable();
		}

		const auto* resourceSnapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<DX12ResourceManagerSnapshot>()
			: nullptr;
		DrawResourceSummary(resourceSnapshot);

		ImGui::SeparatorText("Pipelines");
		const auto* pipelineSnapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<RHIPipelineSystemSnapshot>()
			: nullptr;
		if (pipelineSnapshot && pipelineSnapshot->m_BackendName == "Direct3D 12")
		{
			ImGui::Text("Pipelines: %u graphics / %u compute | binding layouts %u",
				pipelineSnapshot->m_Cache.m_BackendGraphicsPipelines,
				pipelineSnapshot->m_Cache.m_BackendComputePipelines,
				pipelineSnapshot->m_Cache.m_BackendBindingLayouts);
		}
		else
		{
			ImGui::TextDisabled("Pipeline diagnostics are unavailable.");
		}

		ImGui::SeparatorText("Frame transaction");
		ImGui::Text("Swapchain: %ux%u, %u buffers, %s, VSync %s, tearing %s",
			snapshot->m_SwapChainWidth, snapshot->m_SwapChainHeight,
			snapshot->m_SwapChainBufferCount, snapshot->m_SwapChainFormat.c_str(),
			snapshot->m_Vsync ? "on" : "off", snapshot->m_AllowTearing ? "allowed" : "off");
		ImGui::Text("Frame slots: %u | backbuffer: %u / %u", snapshot->m_FrameSlotCount,
			snapshot->m_BackBufferIndex, snapshot->m_SwapChainBufferCount);
		ImGui::Text("Graphics fence: submitted %llu, completed %llu",
			static_cast<unsigned long long>(snapshot->m_SubmittedGraphicsFence),
			static_cast<unsigned long long>(snapshot->m_CompletedGraphicsFence));
		ImGui::Text("Runtime health: %s", snapshot->m_DeviceHealthy ? "healthy" : "device removed");
		if (!snapshot->m_DeviceHealthy)
		{
			ImGui::Text("Device removed reason: HRESULT 0x%08X",
				static_cast<uint32_t>(snapshot->m_DeviceRemovedReason));
		}
	}
}
