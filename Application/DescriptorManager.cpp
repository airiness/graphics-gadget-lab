#include "Precompiled.h"
#include "DescriptorManager.h"
#include "DX12Descriptor.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	DescriptorManager::DescriptorManager(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_DX12Device);

		DX12DescriptorHeap::CreateInfo heapCreateInfo{};
		heapCreateInfo.m_DX12Device = createInfo.m_DX12Device;

		heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapCreateInfo.m_DescriptorCount = 1024;
		m_CbvSrvUavHeap = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

		DX12DescriptorAllocatorBase::CreateInfo descriptorCreateInfo{};
		descriptorCreateInfo.m_DescriptorHeap = m_CbvSrvUavHeap.get();
		m_CbvSrvUavDescriptorAllocator =
			std::make_unique<DX12DescriptorFreeListAllocator>(descriptorCreateInfo);

		heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapCreateInfo.m_DescriptorCount = 32;
		m_RtvHeap = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

		descriptorCreateInfo.m_DescriptorHeap = m_RtvHeap.get();
		m_RtvDescriptorAllocator =
			std::make_unique<DX12DescriptorFreeListAllocator>(descriptorCreateInfo);

		heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		heapCreateInfo.m_DescriptorCount = 16;
		m_DsvHeap = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

		descriptorCreateInfo.m_DescriptorHeap = m_DsvHeap.get();
		m_DsvDescriptorAllocator =
			std::make_unique<DX12DescriptorFreeListAllocator>(descriptorCreateInfo);

		heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		heapCreateInfo.m_DescriptorCount = 256;
		m_SamplerHeap = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

		descriptorCreateInfo.m_DescriptorHeap = m_SamplerHeap.get();
		m_SamplerDescriptorAllocator =
			std::make_unique<DX12DescriptorFreeListAllocator>(descriptorCreateInfo);
	}

	void DescriptorManager::Tick() noexcept
	{
	}

	void DescriptorManager::EndFrame(const DX12FencePoint& fencePoint) noexcept
	{
	}
}