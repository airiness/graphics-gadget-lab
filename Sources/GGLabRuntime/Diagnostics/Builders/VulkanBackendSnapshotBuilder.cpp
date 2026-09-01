#include "Diagnostics/Builders/VulkanBackendSnapshotBuilder.h"
#include "Diagnostics/Builders/RHIPipelineSystemSnapshotBuilder.h"
#include "Diagnostics/Snapshots/RHIPipelineSystemSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/VulkanBackendSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIFormat.h"
#include "Graphics/RHI/Vulkan/VulkanBootstrap.h"
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#include "Graphics/RHI/Vulkan/VulkanDescriptorManager.h"
#include "Graphics/RHI/Vulkan/VulkanFrameRuntime.h"
#include "Graphics/RHI/Vulkan/VulkanGpuProfiler.h"
#include "Graphics/RHI/Vulkan/VulkanInstance.h"
#include "Graphics/RHI/Vulkan/VulkanPipelineSystem.h"
#include "Graphics/RHI/Vulkan/VulkanResourceManager.h"
#include "Graphics/RHI/Vulkan/VulkanShaderBindingABI.h"
#include "Graphics/RHI/Vulkan/VulkanSwapChain.h"
#include "Graphics/RHI/Vulkan/VulkanUtility.h"

#include <vk_mem_alloc.h>

namespace gglab
{
	namespace
	{
		VulkanDescriptorArenaSnapshot MakeDescriptorSnapshot(
			const VulkanDescriptorPublicationDiagnostics& source) noexcept
		{
			return {
				.m_Capacity = source.m_Capacity,
				.m_FreeCount = source.m_FreeCount,
				.m_AllocatedUnpublishedCount = source.m_AllocatedUnpublishedCount,
				.m_DescriptorReadyCount = source.m_DescriptorReadyCount,
				.m_LiveCount = source.m_LiveCount,
				.m_RetiredCount = source.m_RetiredCount,
				.m_RetirementRequestedCount = source.m_RetirementRequestedCount,
				.m_RetainedBackingCount = source.m_RetainedBackingCount,
				.m_EstimatedRetainedBytes = source.m_EstimatedRetainedBytes,
				.m_HighWaterMark = source.m_HighWaterMark,
				.m_InvalidTransitionCount = source.m_InvalidTransitionCount,
			};
		}

		VulkanResourceManagerSnapshot MakeResourceManagerSnapshot(
			const VulkanResourceManager::RuntimeDiagnostics& source) noexcept
		{
			return {
				.m_LiveTextures = source.m_LiveTextures,
				.m_RetiredTextures = source.m_RetiredTextures,
				.m_LiveBuffers = source.m_LiveBuffers,
				.m_RetiredBuffers = source.m_RetiredBuffers,
				.m_LiveTextureViews = source.m_LiveTextureViews,
				.m_RetiredTextureViews = source.m_RetiredTextureViews,
				.m_LiveBufferViews = source.m_LiveBufferViews,
				.m_RetiredBufferViews = source.m_RetiredBufferViews,
				.m_LiveSamplers = source.m_LiveSamplers,
				.m_RetiredSamplers = source.m_RetiredSamplers,
				.m_LiveAllocationBytes = source.m_LiveAllocationBytes,
				.m_RetiredAllocationBytes = source.m_RetiredAllocationBytes,
				.m_TextureCreateCount = source.m_TextureCreateCount,
				.m_BufferCreateCount = source.m_BufferCreateCount,
				.m_TextureImportCount = source.m_TextureImportCount,
				.m_BufferImportCount = source.m_BufferImportCount,
				.m_TextureRetireCount = source.m_TextureRetireCount,
				.m_BufferRetireCount = source.m_BufferRetireCount,
				.m_CreateFailureCount = source.m_CreateFailureCount,
				.m_ImportFailureCount = source.m_ImportFailureCount,
				.m_InvalidUseCount = source.m_InvalidUseCount,
				.m_InvalidDestroyCount = source.m_InvalidDestroyCount,
				.m_StaleDestroyCount = source.m_StaleDestroyCount,
				.m_DoubleDestroyCount = source.m_DoubleDestroyCount,
			};
		}

		const char* PresentModeText(VkPresentModeKHR mode) noexcept
		{
			switch (mode)
			{
			case VK_PRESENT_MODE_IMMEDIATE_KHR:
				return "Immediate";
			case VK_PRESENT_MODE_MAILBOX_KHR:
				return "Mailbox";
			case VK_PRESENT_MODE_FIFO_KHR:
				return "FIFO";
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
				return "FIFO relaxed";
			default:
				return "Other";
			}
		}
	}

	void BuildVulkanBackendSnapshot(
		const VulkanContext& context, VulkanBackendSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		if (!context.m_Bootstrap || !context.m_Bootstrap->m_Device ||
			!context.m_Bootstrap->m_FrameRuntime)
		{
			return;
		}

		outSnapshot.m_Available = true;
		const auto& adapter = context.m_Bootstrap->m_SelectedSnapshot;
		const auto& identity = adapter.m_Identity;
		const auto& capabilities = adapter.m_ProfileCapabilities;
		const VulkanDevice& device = *context.m_Bootstrap->m_Device;
		const VulkanFrameRuntime& runtime = *context.m_Bootstrap->m_FrameRuntime;
		const VulkanSwapChain& swapChain = runtime.GetSwapChain();

		outSnapshot.m_DeviceName = identity.m_DeviceName;
		outSnapshot.m_DriverName = identity.m_DriverName;
		outSnapshot.m_DriverInfo = identity.m_DriverInfo;
		outSnapshot.m_DeviceUuid = identity.UuidHex();
		outSnapshot.m_ApiVersion = identity.m_ApiVersion;
		outSnapshot.m_DriverVersion = identity.m_DriverVersion;
		outSnapshot.m_VendorId = identity.m_VendorId;
		outSnapshot.m_DeviceId = identity.m_DeviceId;
		outSnapshot.m_GraphicsPresentQueueFamilyIndex =
			adapter.m_GraphicsPresentQueueFamilyIndex;
		outSnapshot.m_GraphicsPresentQueueCount = adapter.m_GraphicsPresentQueueCount;
		outSnapshot.m_GraphicsQueueIndex = device.GetGraphicsQueueIndex();
		outSnapshot.m_TransferQueueIndex = device.GetTransferQueueIndex();
		outSnapshot.m_SeparateTransferQueue = device.HasSeparateTransferQueue();
		outSnapshot.m_ProfileAccepted = adapter.m_ProfileEvaluation.IsAccepted();
		outSnapshot.m_DynamicRendering = capabilities.m_DynamicRendering;
		outSnapshot.m_Synchronization2 = capabilities.m_Synchronization2;
		outSnapshot.m_TimelineSemaphore = capabilities.m_TimelineSemaphore;
		outSnapshot.m_RuntimeDescriptorArray = capabilities.m_RuntimeDescriptorArray;
		outSnapshot.m_DescriptorBindingPartiallyBound =
			capabilities.m_DescriptorBindingPartiallyBound;
		outSnapshot.m_MutableDescriptorType = capabilities.m_MutableDescriptorType;
		for (const VulkanDescriptorType type :
			GGLabVulkanShaderBindingABI.m_ResourceHeapMutableAllowedTypes)
		{
			switch (type)
			{
			case VulkanDescriptorType::SampledImage:
				outSnapshot.m_MutableResourceDescriptorTypes.emplace_back("Sampled image");
				break;
			case VulkanDescriptorType::StorageImage:
				outSnapshot.m_MutableResourceDescriptorTypes.emplace_back("Storage image");
				break;
			default:
				outSnapshot.m_MutableResourceDescriptorTypes.emplace_back("Unexpected type");
				break;
			}
		}

		outSnapshot.m_ValidationRequested = context.m_ValidationRequested;
		outSnapshot.m_ValidationEnabled = context.m_Bootstrap->m_HasDebugMessenger;
		if (context.m_Bootstrap->m_Instance)
		{
			const VulkanValidationDiagnostics validation =
				context.m_Bootstrap->m_Instance->GetValidationDiagnostics();
			outSnapshot.m_ValidationErrors = validation.m_ErrorCount;
			outSnapshot.m_ValidationWarnings = validation.m_WarningCount;
			outSnapshot.m_ValidationInfo = validation.m_InfoCount;
			outSnapshot.m_ValidationVerbose = validation.m_VerboseCount;
		}

		const VulkanDescriptorManager& descriptors = device.GetDescriptorManager();
		outSnapshot.m_Resources = MakeDescriptorSnapshot(descriptors.GetResourceDiagnostics());
		outSnapshot.m_Samplers = MakeDescriptorSnapshot(descriptors.GetSamplerDiagnostics());
		outSnapshot.m_ResourceManager = MakeResourceManagerSnapshot(
			device.GetResourceManager().GetRuntimeDiagnostics());

		VmaAllocator allocator = device.GetMemAllocator();
		if (allocator != VK_NULL_HANDLE)
		{
			VmaTotalStatistics statistics{};
			vmaCalculateStatistics(allocator, &statistics);
			outSnapshot.m_TotalVmaBlockBytes = statistics.total.statistics.blockBytes;
			outSnapshot.m_TotalVmaAllocationBytes = statistics.total.statistics.allocationBytes;
			outSnapshot.m_TotalVmaAllocationCount = statistics.total.statistics.allocationCount;

			VkPhysicalDeviceMemoryProperties memoryProperties{};
			vkGetPhysicalDeviceMemoryProperties(device.GetPhysicalDevice(), &memoryProperties);
			VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
			vmaGetHeapBudgets(allocator, budgets);
			outSnapshot.m_MemoryHeaps.reserve(memoryProperties.memoryHeapCount);
			for (uint32_t index = 0; index < memoryProperties.memoryHeapCount; ++index)
			{
				outSnapshot.m_MemoryHeaps.push_back({
					.m_Index = index,
					.m_DeviceLocal = (memoryProperties.memoryHeaps[index].flags &
						VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0,
					.m_HeapSize = memoryProperties.memoryHeaps[index].size,
					.m_Budget = budgets[index].budget,
					.m_Usage = budgets[index].usage,
					.m_BlockBytes = budgets[index].statistics.blockBytes,
					.m_AllocationBytes = budgets[index].statistics.allocationBytes,
					});
			}
		}

		if (context.m_PipelineSystem)
		{
			RHIPipelineSystemSnapshot pipelines{};
			BuildVulkanPipelineSystemSnapshot(*context.m_PipelineSystem, nullptr, pipelines);
			outSnapshot.m_GraphicsPipelineCount =
				pipelines.m_Cache.m_BackendGraphicsPipelines;
			outSnapshot.m_ComputePipelineCount =
				pipelines.m_Cache.m_BackendComputePipelines;
			outSnapshot.m_BindingLayoutCount = pipelines.m_Cache.m_BackendBindingLayouts;
		}
		outSnapshot.m_GpuProfilerAvailable =
			context.m_GpuProfiler && context.m_GpuProfiler->IsAvailable();
		outSnapshot.m_GpuProfilerEnabled =
			context.m_GpuProfiler && context.m_GpuProfiler->IsEnabled();
		outSnapshot.m_NativePipelineCachePersistenceEnabled = false;

		outSnapshot.m_FrameSlotCount = runtime.GetFrameSlotCount();
		const auto& framePairs = runtime.GetIndexModel().GetFramePairs();
		if (!framePairs.empty())
		{
			outSnapshot.m_HasFramePair = true;
			outSnapshot.m_FrameSlotIndex = framePairs.back().first;
			outSnapshot.m_BackBufferIndex = framePairs.back().second;
		}
		outSnapshot.m_SwapChainImageCount = swapChain.GetImageCount();
		outSnapshot.m_SwapChainWidth = swapChain.GetWidth();
		outSnapshot.m_SwapChainHeight = swapChain.GetHeight();
		outSnapshot.m_SwapChainFormat = GetRHIFormatInfo(swapChain.GetFormat()).m_Name;
		outSnapshot.m_PresentMode = PresentModeText(swapChain.GetPresentMode());
		outSnapshot.m_Vsync = swapChain.GetVsync();
		outSnapshot.m_SwapChainGeneration = context.m_SwapChainGeneration;
		outSnapshot.m_SubmittedTimeline = runtime.GetTimelineSignalValue();
		outSnapshot.m_HasCompletedTimeline =
			runtime.GetTimeline().GetCompletedValue(outSnapshot.m_CompletedTimeline) == VK_SUCCESS;
		if (const VulkanTimelineFence* transferTimeline = device.GetTransferTimeline())
		{
			outSnapshot.m_TransferSubmittedTimeline =
				transferTimeline->GetCurrentSignalValue();
			outSnapshot.m_HasCompletedTransferTimeline = transferTimeline->GetCompletedValue(
				outSnapshot.m_TransferCompletedTimeline) == VK_SUCCESS;
		}

		outSnapshot.m_RuntimeFatal = runtime.IsFatal();
		outSnapshot.m_DeviceLost = runtime.IsDeviceLost();
		const VulkanRuntimeFailureDiagnostics& failure = runtime.GetFailureDiagnostics();
		outSnapshot.m_FailingOperation = failure.m_Operation;
		outSnapshot.m_FailingResult = ToString(failure.m_Result);
		outSnapshot.m_FailingResultCode = static_cast<int32_t>(failure.m_Result);
		outSnapshot.m_LastSubmissionBeforeFailure = failure.m_LastSubmittedTimelineValue;
	}
}
