#pragma once
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gglab
{
	struct VulkanDescriptorArenaSnapshot
	{
		uint32_t m_Capacity = 0;
		uint32_t m_FreeCount = 0;
		uint32_t m_AllocatedUnpublishedCount = 0;
		uint32_t m_DescriptorReadyCount = 0;
		uint32_t m_LiveCount = 0;
		uint32_t m_RetiredCount = 0;
		uint32_t m_RetirementRequestedCount = 0;
		uint32_t m_RetainedBackingCount = 0;
		uint64_t m_EstimatedRetainedBytes = 0;
		uint32_t m_HighWaterMark = 0;
		uint64_t m_InvalidTransitionCount = 0;
	};

	struct VulkanMemoryHeapSnapshot
	{
		uint32_t m_Index = 0;
		bool m_DeviceLocal = false;
		uint64_t m_HeapSize = 0;
		uint64_t m_Budget = 0;
		uint64_t m_Usage = 0;
		uint64_t m_BlockBytes = 0;
		uint64_t m_AllocationBytes = 0;
	};

	struct VulkanResourceManagerSnapshot
	{
		uint32_t m_LiveTextures = 0;
		uint32_t m_RetiredTextures = 0;
		uint32_t m_LiveBuffers = 0;
		uint32_t m_RetiredBuffers = 0;
		uint32_t m_LiveTextureViews = 0;
		uint32_t m_RetiredTextureViews = 0;
		uint32_t m_LiveBufferViews = 0;
		uint32_t m_RetiredBufferViews = 0;
		uint32_t m_LiveSamplers = 0;
		uint32_t m_RetiredSamplers = 0;
		uint64_t m_LiveAllocationBytes = 0;
		uint64_t m_RetiredAllocationBytes = 0;
		uint64_t m_TextureCreateCount = 0;
		uint64_t m_BufferCreateCount = 0;
		uint64_t m_TextureImportCount = 0;
		uint64_t m_BufferImportCount = 0;
		uint64_t m_TextureRetireCount = 0;
		uint64_t m_BufferRetireCount = 0;
		uint64_t m_CreateFailureCount = 0;
		uint64_t m_ImportFailureCount = 0;
		uint64_t m_InvalidUseCount = 0;
		uint64_t m_InvalidDestroyCount = 0;
		uint64_t m_StaleDestroyCount = 0;
		uint64_t m_DoubleDestroyCount = 0;
	};

	struct VulkanBackendSnapshot
	{
		bool m_Available = false;
		std::string m_DeviceName;
		std::string m_DriverName;
		std::string m_DriverInfo;
		std::string m_DeviceUuid;
		uint32_t m_ApiVersion = 0;
		uint32_t m_DriverVersion = 0;
		uint32_t m_VendorId = 0;
		uint32_t m_DeviceId = 0;
		uint32_t m_GraphicsPresentQueueFamilyIndex = 0;
		uint32_t m_GraphicsPresentQueueCount = 0;
		uint32_t m_GraphicsQueueIndex = 0;
		uint32_t m_TransferQueueIndex = 0;
		bool m_SeparateTransferQueue = false;
		bool m_ProfileAccepted = false;
		bool m_DynamicRendering = false;
		bool m_Synchronization2 = false;
		bool m_TimelineSemaphore = false;
		bool m_RuntimeDescriptorArray = false;
		bool m_DescriptorBindingPartiallyBound = false;
		bool m_MutableDescriptorType = false;
		std::vector<std::string> m_MutableResourceDescriptorTypes;

		bool m_ValidationRequested = false;
		bool m_ValidationEnabled = false;
		uint64_t m_ValidationErrors = 0;
		uint64_t m_ValidationWarnings = 0;
		uint64_t m_ValidationInfo = 0;
		uint64_t m_ValidationVerbose = 0;

		VulkanDescriptorArenaSnapshot m_Resources;
		VulkanDescriptorArenaSnapshot m_Samplers;
		VulkanResourceManagerSnapshot m_ResourceManager;
		std::vector<VulkanMemoryHeapSnapshot> m_MemoryHeaps;
		uint64_t m_TotalVmaBlockBytes = 0;
		uint64_t m_TotalVmaAllocationBytes = 0;
		uint32_t m_TotalVmaAllocationCount = 0;

		uint32_t m_GraphicsPipelineCount = 0;
		uint32_t m_ComputePipelineCount = 0;
		uint32_t m_BindingLayoutCount = 0;
		bool m_GpuProfilerAvailable = false;
		bool m_GpuProfilerEnabled = false;
		bool m_NativePipelineCachePersistenceEnabled = false;

		uint32_t m_FrameSlotCount = 0;
		bool m_HasFramePair = false;
		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		uint32_t m_SwapChainImageCount = 0;
		uint32_t m_SwapChainWidth = 0;
		uint32_t m_SwapChainHeight = 0;
		std::string m_SwapChainFormat;
		std::string m_PresentMode;
		bool m_Vsync = false;
		uint64_t m_SwapChainGeneration = 0;
		uint64_t m_SubmittedTimeline = 0;
		bool m_HasCompletedTimeline = false;
		uint64_t m_CompletedTimeline = 0;
		uint64_t m_TransferSubmittedTimeline = 0;
		bool m_HasCompletedTransferTimeline = false;
		uint64_t m_TransferCompletedTimeline = 0;

		bool m_RuntimeFatal = false;
		bool m_DeviceLost = false;
		std::string m_FailingOperation;
		std::string m_FailingResult;
		int32_t m_FailingResultCode = 0;
		uint64_t m_LastSubmissionBeforeFailure = 0;
	};

	template <> struct SnapshotTraits<VulkanBackendSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.VulkanBackendSnapshot");
	};
}
