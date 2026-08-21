#include "Diagnostics/Builders/DX12BackendSnapshotBuilder.h"
#include "Diagnostics/Snapshots/DX12BackendSnapshot.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Fence.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12SwapChain.h"
#include "Graphics/RHI/RHIFormat.h"

#include <D3D12MemAlloc.h>
#include <array>

namespace gglab
{
	namespace
	{
		DX12MemorySegmentSnapshot MakeMemorySegmentSnapshot(
			const D3D12MA::Budget& budget) noexcept
		{
			return {
				.m_BudgetBytes = budget.BudgetBytes,
				.m_UsageBytes = budget.UsageBytes,
				.m_BlockBytes = budget.Stats.BlockBytes,
				.m_AllocationBytes = budget.Stats.AllocationBytes,
				.m_BlockCount = budget.Stats.BlockCount,
				.m_AllocationCount = budget.Stats.AllocationCount,
			};
		}

		uint32_t QueryFeatureLevel(ID3D12Device* device) noexcept
		{
			constexpr std::array featureLevels{
				D3D_FEATURE_LEVEL_12_2,
				D3D_FEATURE_LEVEL_12_1,
				D3D_FEATURE_LEVEL_12_0,
			};
			D3D12_FEATURE_DATA_FEATURE_LEVELS query{
				.NumFeatureLevels = static_cast<UINT>(featureLevels.size()),
				.pFeatureLevelsRequested = featureLevels.data(),
			};
			return device && SUCCEEDED(device->CheckFeatureSupport(
				D3D12_FEATURE_FEATURE_LEVELS, &query, sizeof(query)))
				? static_cast<uint32_t>(query.MaxSupportedFeatureLevel)
				: 0;
		}
	}

	void BuildDX12BackendSnapshot(
		const DX12Context& context, DX12BackendSnapshot& outSnapshot) noexcept
	{
		outSnapshot = {};
		const DX12Device& device = context.GetDX12Device();
		ID3D12Device* nativeDevice = device.Get();
		IDXGIAdapter1* adapter = device.GetDXGIAdapter();
		if (!nativeDevice || !adapter)
		{
			return;
		}

		outSnapshot.m_Available = true;
		outSnapshot.m_DeviceName = device.GetAdapterName();
		DXGI_ADAPTER_DESC1 adapterDesc{};
		if (SUCCEEDED(adapter->GetDesc1(&adapterDesc)))
		{
			outSnapshot.m_VendorId = adapterDesc.VendorId;
			outSnapshot.m_DeviceId = adapterDesc.DeviceId;
			outSnapshot.m_SubSystemId = adapterDesc.SubSysId;
			outSnapshot.m_Revision = adapterDesc.Revision;
			outSnapshot.m_AdapterLuidHigh = adapterDesc.AdapterLuid.HighPart;
			outSnapshot.m_AdapterLuidLow = adapterDesc.AdapterLuid.LowPart;
			outSnapshot.m_DedicatedVideoMemory = adapterDesc.DedicatedVideoMemory;
			outSnapshot.m_DedicatedSystemMemory = adapterDesc.DedicatedSystemMemory;
			outSnapshot.m_SharedSystemMemory = adapterDesc.SharedSystemMemory;
		}

		LARGE_INTEGER driverVersion{};
		if (SUCCEEDED(
			adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &driverVersion)))
		{
			const DWORD high = static_cast<DWORD>(driverVersion.HighPart);
			const DWORD low = driverVersion.LowPart;
			outSnapshot.m_HasDriverVersion = true;
			outSnapshot.m_DriverProduct = HIWORD(high);
			outSnapshot.m_DriverVersion = LOWORD(high);
			outSnapshot.m_DriverSubVersion = HIWORD(low);
			outSnapshot.m_DriverBuild = LOWORD(low);
		}

		if (D3D12MA::Allocator* allocator = device.GetMemAllocator())
		{
			D3D12MA::Budget localBudget{};
			D3D12MA::Budget nonLocalBudget{};
			allocator->GetBudget(&localBudget, &nonLocalBudget);
			outSnapshot.m_IsUma = allocator->IsUMA() != FALSE;
			outSnapshot.m_IsCacheCoherentUma = allocator->IsCacheCoherentUMA() != FALSE;
			outSnapshot.m_LocalMemory = MakeMemorySegmentSnapshot(localBudget);
			outSnapshot.m_NonLocalMemory = MakeMemorySegmentSnapshot(nonLocalBudget);
		}

		outSnapshot.m_FeatureLevel = QueryFeatureLevel(nativeDevice);
		outSnapshot.m_RayTracingSupported = device.SupportRayTracing();
		outSnapshot.m_MeshShaderSupported = device.SupportMeshShader();
		outSnapshot.m_EnhancedBarriersSupported = device.SupportEnhancedBarrier();
		outSnapshot.m_TearingSupported = device.SupportTearing();
		const RHIShaderWaveCapabilities waveCapabilities = device.GetShaderWaveCapabilities();
		outSnapshot.m_WaveOperationsSupported = waveCapabilities.m_Supported;
		outSnapshot.m_MinWaveLaneCount = waveCapabilities.m_MinLaneCount;
		outSnapshot.m_MaxWaveLaneCount = waveCapabilities.m_MaxLaneCount;
		const GpuProfiler* gpuProfiler = context.GetGpuProfiler();
		outSnapshot.m_GpuProfilerEnabled = gpuProfiler && gpuProfiler->IsEnabled();

		const DX12SwapChain& swapChain =
			static_cast<const DX12SwapChain&>(context.GetSwapChain());
		outSnapshot.m_FrameSlotCount = context.GetFrameSlotCount();
		outSnapshot.m_BackBufferIndex = swapChain.GetCurrentBackBufferIndex();
		outSnapshot.m_SwapChainBufferCount = swapChain.GetBufferCount();
		outSnapshot.m_SwapChainWidth = swapChain.GetBufferWidth();
		outSnapshot.m_SwapChainHeight = swapChain.GetBufferHeight();
		outSnapshot.m_SwapChainFormat = GetRHIFormatInfo(swapChain.GetFormat()).m_Name;
		outSnapshot.m_Vsync = swapChain.GetVsync();
		outSnapshot.m_AllowTearing = swapChain.GetAllowTearing();

		const DX12CommandQueue& graphicsQueue =
			context.GetQueueSystem().GetQueue(DX12QueueType::Graphics);
		if (const DX12Fence* fence = graphicsQueue.GetFence())
		{
			outSnapshot.m_SubmittedGraphicsFence = fence->GetCurrentValue();
			outSnapshot.m_CompletedGraphicsFence = fence->GetCompletedValue();
		}

		const auto deviceRemovedReason = nativeDevice->GetDeviceRemovedReason();
		outSnapshot.m_DeviceHealthy = SUCCEEDED(deviceRemovedReason);
		outSnapshot.m_DeviceRemovedReason = static_cast<int32_t>(deviceRemovedReason);
	}
}
