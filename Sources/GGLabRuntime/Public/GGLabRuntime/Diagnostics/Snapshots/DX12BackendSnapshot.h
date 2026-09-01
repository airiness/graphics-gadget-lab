#pragma once
#include "GGLabRuntime/Diagnostics/SnapshotCommon.h"

#include <cstdint>
#include <string>

namespace gglab
{
	struct DX12MemorySegmentSnapshot
	{
		uint64_t m_BudgetBytes = 0;
		uint64_t m_UsageBytes = 0;
		uint64_t m_BlockBytes = 0;
		uint64_t m_AllocationBytes = 0;
		uint32_t m_BlockCount = 0;
		uint32_t m_AllocationCount = 0;
	};

	struct DX12BackendSnapshot
	{
		bool m_Available = false;
		std::string m_DeviceName;
		uint32_t m_VendorId = 0;
		uint32_t m_DeviceId = 0;
		uint32_t m_SubSystemId = 0;
		uint32_t m_Revision = 0;
		int32_t m_AdapterLuidHigh = 0;
		uint32_t m_AdapterLuidLow = 0;
		bool m_HasDriverVersion = false;
		uint16_t m_DriverProduct = 0;
		uint16_t m_DriverVersion = 0;
		uint16_t m_DriverSubVersion = 0;
		uint16_t m_DriverBuild = 0;

		uint64_t m_DedicatedVideoMemory = 0;
		uint64_t m_DedicatedSystemMemory = 0;
		uint64_t m_SharedSystemMemory = 0;
		bool m_IsUma = false;
		bool m_IsCacheCoherentUma = false;
		DX12MemorySegmentSnapshot m_LocalMemory;
		DX12MemorySegmentSnapshot m_NonLocalMemory;

		uint32_t m_FeatureLevel = 0;
		bool m_RayTracingSupported = false;
		bool m_MeshShaderSupported = false;
		bool m_EnhancedBarriersSupported = false;
		bool m_TearingSupported = false;
		bool m_WaveOperationsSupported = false;
		uint32_t m_MinWaveLaneCount = 0;
		uint32_t m_MaxWaveLaneCount = 0;
		bool m_GpuProfilerEnabled = false;

		uint32_t m_FrameSlotCount = 0;
		uint32_t m_BackBufferIndex = 0;
		uint32_t m_SwapChainBufferCount = 0;
		uint32_t m_SwapChainWidth = 0;
		uint32_t m_SwapChainHeight = 0;
		std::string m_SwapChainFormat;
		bool m_Vsync = false;
		bool m_AllowTearing = false;
		uint64_t m_SubmittedGraphicsFence = 0;
		uint64_t m_CompletedGraphicsFence = 0;

		bool m_DeviceHealthy = false;
		int32_t m_DeviceRemovedReason = 0;
	};

	template <> struct SnapshotTraits<DX12BackendSnapshot>
	{
		static constexpr SnapshotId Id = MakeSnapshotId("Diagnostics.DX12BackendSnapshot");
	};
}
