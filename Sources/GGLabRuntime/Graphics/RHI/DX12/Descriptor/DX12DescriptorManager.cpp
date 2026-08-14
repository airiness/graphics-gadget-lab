#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorManager.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorHeap.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorFreeListAllocator.h"

#include <memory>

namespace gglab
{
	DX12DescriptorManager::DX12DescriptorManager(const CreateInfo& createInfo) noexcept :
		m_DX12Device(createInfo.m_DX12Device)
	{
		GGLAB_ASSERT_NOT_NULL(m_DX12Device);

		// Create Descriptor Heaps
		{
			DX12DescriptorHeap::CreateInfo heapCreateInfo{};
			heapCreateInfo.m_DX12Device = m_DX12Device;

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_CbvSrvUavCount;
			m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)] =
				std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_RtvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Rtv)] =
				std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_DsvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Dsv)] =
				std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_SamplerCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Sampler)] =
				std::make_unique<DX12DescriptorHeap>(heapCreateInfo);
		}

		// Create Descriptor Allocators
		{
			const uint32_t totalSrvCount = createInfo.m_CbvSrvUavCount;
			const uint32_t developGuiSrvCount = createInfo.m_DevelopGuiSrvCount;

			GGLAB_ASSERT_MSG(developGuiSrvCount < totalSrvCount, "Srv range overflow.");

			// DevelopGui Srv
			DX12DescriptorAllocatorBase::CreateInfo allocatorCreateInfo{};
			allocatorCreateInfo.m_DescriptorHeap =
				m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)].get();
			allocatorCreateInfo.m_Range = { 0, developGuiSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::DevelopGuiSrv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Srv
			allocatorCreateInfo.m_Range = {
				developGuiSrvCount, createInfo.m_CbvSrvUavCount - developGuiSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralCbvSrvUav)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Rtv
			allocatorCreateInfo.m_DescriptorHeap =
				m_Heaps[static_cast<uint8_t>(HeapType::Rtv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_RtvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralRtv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Dsv
			allocatorCreateInfo.m_DescriptorHeap =
				m_Heaps[static_cast<uint8_t>(HeapType::Dsv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_DsvCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralDsv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Sampler
			allocatorCreateInfo.m_DescriptorHeap =
				m_Heaps[static_cast<uint8_t>(HeapType::Sampler)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_SamplerCount };
			m_FreeListAllocators[static_cast<uint8_t>(AllocatorType::GeneralSampler)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);
		}
	}

	DX12DescriptorManager::~DX12DescriptorManager() = default;

	void DX12DescriptorManager::Tick() noexcept
	{
		for (auto& allocator : m_FreeListAllocators)
		{
			if (allocator)
			{
				allocator->Tick();
			}
		}
	}

	void DX12DescriptorManager::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
		for (auto& allocator : m_FreeListAllocators)
		{
			if (allocator)
			{
				allocator->EndFrame(fencePoint);
			}
		}
	}

	DX12DescriptorHeap* DX12DescriptorManager::GetHeap(HeapType heapType) const noexcept
	{
		return m_Heaps[utils::ToIndexChecked(heapType)].get();
	}

	DX12DescriptorFreeListAllocator* DX12DescriptorManager::GetFreeListAllocator(
		AllocatorType allocatorType) const noexcept
	{
		return m_FreeListAllocators[utils::ToIndexChecked(allocatorType)].get();
	}

	DX12DescriptorView DX12DescriptorManager::AllocateDevelopGuiSrvView() noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		return allocator->AllocateView();
	}

	void DX12DescriptorManager::DeferFreeDevelopGuiSrvInFrame(
		D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle) noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		allocator->DeferFreeFromCpuHandleInFrame(cpuHandle);
	}

	void DX12DescriptorManager::DeferFreeDevelopGuiSrvInFrame(
		D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) noexcept
	{
		auto* allocator = GetFreeListAllocator(AllocatorType::DevelopGuiSrv);
		GGLAB_ASSERT_NOT_NULL(allocator);

		allocator->DeferFreeFromGpuHandleInFrame(gpuHandle);
	}

}
