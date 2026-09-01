#include "DevTools/DevelopGui/Panels/VulkanBackendSummaryPanel.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "Diagnostics/DiagnosticsRuntime.h"
#include "GGLabRuntime/Diagnostics/Snapshots/VulkanBackendSnapshot.h"

#include <imgui.h>
#include <vulkan/vulkan.h>

namespace gglab
{
	namespace
	{
		const char* EnabledText(bool enabled) noexcept
		{
			return enabled ? "enabled" : "disabled";
		}

		const char* AvailabilityText(bool available) noexcept
		{
			return available ? "available" : "unavailable";
		}

		double MiB(uint64_t bytes) noexcept
		{
			return static_cast<double>(bytes) / (1024.0 * 1024.0);
		}

		void DrawDescriptorSummary(
			const char* label, const VulkanDescriptorArenaSnapshot& diagnostics) noexcept
		{
			ImGui::Text("%s: %u live, %u retired, %u free / %u", label,
				diagnostics.m_LiveCount, diagnostics.m_RetiredCount,
				diagnostics.m_FreeCount, diagnostics.m_Capacity);
			ImGui::TextDisabled(
				"  allocated-unpublished %u, ready %u, retirement requested %u",
				diagnostics.m_AllocatedUnpublishedCount,
				diagnostics.m_DescriptorReadyCount,
				diagnostics.m_RetirementRequestedCount);
			ImGui::TextDisabled(
				"  high-water %u, retained backings %u (%.2f MiB), invalid transitions %llu",
				diagnostics.m_HighWaterMark, diagnostics.m_RetainedBackingCount,
				MiB(diagnostics.m_EstimatedRetainedBytes),
				static_cast<unsigned long long>(diagnostics.m_InvalidTransitionCount));
		}

		void DrawMemory(const VulkanBackendSnapshot& snapshot) noexcept
		{
			ImGui::SeparatorText("VMA totals");
			ImGui::Text("Blocks %.2f MiB | Allocations %.2f MiB in %u allocations",
				MiB(snapshot.m_TotalVmaBlockBytes), MiB(snapshot.m_TotalVmaAllocationBytes),
				snapshot.m_TotalVmaAllocationCount);
			if (ImGui::BeginTable("VulkanMemoryHeaps", 7,
				ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
			{
				ImGui::TableSetupColumn("Heap");
				ImGui::TableSetupColumn("Class");
				ImGui::TableSetupColumn("Size MiB");
				ImGui::TableSetupColumn("Budget MiB");
				ImGui::TableSetupColumn("Usage MiB");
				ImGui::TableSetupColumn("Blocks MiB");
				ImGui::TableSetupColumn("Alloc MiB");
				ImGui::TableHeadersRow();
				for (const auto& heap : snapshot.m_MemoryHeaps)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::Text("%u", heap.m_Index);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(heap.m_DeviceLocal ? "Device local" : "Host visible");
					ImGui::TableSetColumnIndex(2);
					ImGui::Text("%.2f", MiB(heap.m_HeapSize));
					ImGui::TableSetColumnIndex(3);
					ImGui::Text("%.2f", MiB(heap.m_Budget));
					ImGui::TableSetColumnIndex(4);
					ImGui::Text("%.2f", MiB(heap.m_Usage));
					ImGui::TableSetColumnIndex(5);
					ImGui::Text("%.2f", MiB(heap.m_BlockBytes));
					ImGui::TableSetColumnIndex(6);
					ImGui::Text("%.2f", MiB(heap.m_AllocationBytes));
				}
				ImGui::EndTable();
			}

			const auto& resources = snapshot.m_ResourceManager;
			ImGui::SeparatorText("Resource manager");
			ImGui::Text("Textures %u live / %u retired | Buffers %u live / %u retired",
				resources.m_LiveTextures, resources.m_RetiredTextures,
				resources.m_LiveBuffers, resources.m_RetiredBuffers);
			ImGui::Text("Texture views %u / %u | Buffer views %u / %u | Samplers %u / %u",
				resources.m_LiveTextureViews, resources.m_RetiredTextureViews,
				resources.m_LiveBufferViews, resources.m_RetiredBufferViews,
				resources.m_LiveSamplers, resources.m_RetiredSamplers);
			ImGui::Text("Allocation ownership: %.2f MiB live / %.2f MiB retiring",
				MiB(resources.m_LiveAllocationBytes), MiB(resources.m_RetiredAllocationBytes));
			ImGui::TextDisabled(
				"Creates %llu textures / %llu buffers | imports %llu / %llu | retires %llu / %llu",
				static_cast<unsigned long long>(resources.m_TextureCreateCount),
				static_cast<unsigned long long>(resources.m_BufferCreateCount),
				static_cast<unsigned long long>(resources.m_TextureImportCount),
				static_cast<unsigned long long>(resources.m_BufferImportCount),
				static_cast<unsigned long long>(resources.m_TextureRetireCount),
				static_cast<unsigned long long>(resources.m_BufferRetireCount));
			ImGui::TextDisabled(
				"Failures create=%llu import=%llu | invalid use=%llu destroy=%llu stale=%llu double=%llu",
				static_cast<unsigned long long>(resources.m_CreateFailureCount),
				static_cast<unsigned long long>(resources.m_ImportFailureCount),
				static_cast<unsigned long long>(resources.m_InvalidUseCount),
				static_cast<unsigned long long>(resources.m_InvalidDestroyCount),
				static_cast<unsigned long long>(resources.m_StaleDestroyCount),
				static_cast<unsigned long long>(resources.m_DoubleDestroyCount));
		}
	}

	void VulkanBackendSummaryPanel::Draw(DevelopGuiContext& context) noexcept
	{
		const auto* snapshot = context.m_Diagnostics
			? context.m_Diagnostics->GetSnapshot<VulkanBackendSnapshot>()
			: nullptr;
		if (!snapshot || !snapshot->m_Available)
		{
			ImGui::TextDisabled("The active RHI backend is not Vulkan.");
			return;
		}

		ImGui::SeparatorText("Adapter");
		ImGui::TextUnformatted(snapshot->m_DeviceName.c_str());
		ImGui::Text("Adapter API: Vulkan %u.%u.%u | Application baseline: Vulkan %u.%u",
			VK_VERSION_MAJOR(snapshot->m_ApiVersion), VK_VERSION_MINOR(snapshot->m_ApiVersion),
			VK_VERSION_PATCH(snapshot->m_ApiVersion), VK_VERSION_MAJOR(VK_API_VERSION_1_3),
			VK_VERSION_MINOR(VK_API_VERSION_1_3));
		ImGui::Text("Vendor: 0x%04X | Device: 0x%04X",
			snapshot->m_VendorId, snapshot->m_DeviceId);
		ImGui::Text("UUID: %s", snapshot->m_DeviceUuid.c_str());
		if (!snapshot->m_DriverName.empty())
		{
			ImGui::Text("Driver: %s (version %u)", snapshot->m_DriverName.c_str(),
				snapshot->m_DriverVersion);
		}
		if (!snapshot->m_DriverInfo.empty())
		{
			ImGui::TextDisabled("%s", snapshot->m_DriverInfo.c_str());
		}
		ImGui::Text("Queue family %u: %u available",
			snapshot->m_GraphicsPresentQueueFamilyIndex,
			snapshot->m_GraphicsPresentQueueCount);
		ImGui::Text("Graphics queue %u | transfer queue %u (%s)",
			snapshot->m_GraphicsQueueIndex, snapshot->m_TransferQueueIndex,
			snapshot->m_SeparateTransferQueue ? "separate" : "shared");

		ImGui::SeparatorText("Profile and validation");
		ImGui::Text("Device profile: %s", snapshot->m_ProfileAccepted ? "accepted" : "rejected");
		ImGui::Text("Validation: requested %s, messenger %s | errors %llu, warnings %llu",
			snapshot->m_ValidationRequested ? "yes" : "no",
			EnabledText(snapshot->m_ValidationEnabled),
			static_cast<unsigned long long>(snapshot->m_ValidationErrors),
			static_cast<unsigned long long>(snapshot->m_ValidationWarnings));
		ImGui::TextDisabled("Validation info %llu, verbose %llu",
			static_cast<unsigned long long>(snapshot->m_ValidationInfo),
			static_cast<unsigned long long>(snapshot->m_ValidationVerbose));
		ImGui::Text("Dynamic rendering: %s | synchronization2: %s | timeline: %s",
			EnabledText(snapshot->m_DynamicRendering),
			EnabledText(snapshot->m_Synchronization2),
			EnabledText(snapshot->m_TimelineSemaphore));
		ImGui::Text("Runtime arrays: %s | partially bound: %s | mutable descriptors: %s",
			EnabledText(snapshot->m_RuntimeDescriptorArray),
			EnabledText(snapshot->m_DescriptorBindingPartiallyBound),
			EnabledText(snapshot->m_MutableDescriptorType));
		std::string allowedTypes;
		for (const std::string& type : snapshot->m_MutableResourceDescriptorTypes)
		{
			if (!allowedTypes.empty())
			{
				allowedTypes += ", ";
			}
			allowedTypes += type;
		}
		ImGui::TextDisabled("Mutable resource descriptor allowed types: %s",
			allowedTypes.empty() ? "none" : allowedTypes.c_str());

		ImGui::SeparatorText("Descriptors");
		DrawDescriptorSummary("Resources", snapshot->m_Resources);
		DrawDescriptorSummary("Samplers", snapshot->m_Samplers);

		DrawMemory(*snapshot);

		ImGui::SeparatorText("Pipelines and profiler");
		ImGui::Text("Pipelines: %u graphics / %u compute | binding layouts %u",
			snapshot->m_GraphicsPipelineCount, snapshot->m_ComputePipelineCount,
			snapshot->m_BindingLayoutCount);
		ImGui::Text("GPU profiler: %s (%s) | native cache persistence: %s",
			AvailabilityText(snapshot->m_GpuProfilerAvailable),
			EnabledText(snapshot->m_GpuProfilerEnabled),
			EnabledText(snapshot->m_NativePipelineCachePersistenceEnabled));
		if (!snapshot->m_NativePipelineCachePersistenceEnabled)
		{
			ImGui::TextDisabled(
				"Vulkan pipeline objects are process-local; native cache persistence is deferred.");
		}

		ImGui::SeparatorText("Frame transaction");
		ImGui::Text("Swapchain: %ux%u, %u images, %s, %s, VSync %s, generation %llu",
			snapshot->m_SwapChainWidth, snapshot->m_SwapChainHeight,
			snapshot->m_SwapChainImageCount, snapshot->m_SwapChainFormat.c_str(),
			snapshot->m_PresentMode.c_str(), snapshot->m_Vsync ? "on" : "off",
			static_cast<unsigned long long>(snapshot->m_SwapChainGeneration));
		if (snapshot->m_HasFramePair)
		{
			ImGui::Text("Frame slot %u / %u | backbuffer %u / %u",
				snapshot->m_FrameSlotIndex, snapshot->m_FrameSlotCount,
				snapshot->m_BackBufferIndex, snapshot->m_SwapChainImageCount);
		}
		else
		{
			ImGui::Text("Frame slots: %u | no acquired frame recorded", snapshot->m_FrameSlotCount);
		}
		if (snapshot->m_HasCompletedTimeline)
		{
			ImGui::Text("Graphics timeline: submitted %llu, completed %llu",
				static_cast<unsigned long long>(snapshot->m_SubmittedTimeline),
				static_cast<unsigned long long>(snapshot->m_CompletedTimeline));
		}
		else
		{
			ImGui::Text("Graphics timeline: submitted %llu, completed unavailable",
				static_cast<unsigned long long>(snapshot->m_SubmittedTimeline));
		}
		if (snapshot->m_HasCompletedTransferTimeline)
		{
			ImGui::Text("Transfer timeline: submitted %llu, completed %llu",
				static_cast<unsigned long long>(snapshot->m_TransferSubmittedTimeline),
				static_cast<unsigned long long>(snapshot->m_TransferCompletedTimeline));
		}
		else
		{
			ImGui::Text("Transfer timeline: submitted %llu, completed unavailable",
				static_cast<unsigned long long>(snapshot->m_TransferSubmittedTimeline));
		}
		ImGui::Text("Runtime health: %s%s",
			snapshot->m_RuntimeFatal ? "fatal" : "healthy",
			snapshot->m_DeviceLost ? " (device lost)" : "");
		if (snapshot->m_RuntimeFatal)
		{
			ImGui::Text("Failure: %s -> %s (%d) | last submitted timeline %llu",
				snapshot->m_FailingOperation.empty() ? "unknown operation"
					: snapshot->m_FailingOperation.c_str(),
				snapshot->m_FailingResult.c_str(), snapshot->m_FailingResultCode,
				static_cast<unsigned long long>(snapshot->m_LastSubmissionBeforeFailure));
		}
	}
}
