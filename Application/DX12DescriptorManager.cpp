#include "Precompiled.h"
#include "DX12DescriptorManager.h"
#include "DX12DescriptorTypes.h"
#include "DX12DescriptorHeap.h"
#include "DX12DescriptorFreeListAllocator.h"

namespace gglab
{
	DX12DescriptorManager::DX12DescriptorManager(const CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_DX12Device);

		// Create Descriptor Heaps
		{
			DX12DescriptorHeap::CreateInfo heapCreateInfo{};
			heapCreateInfo.m_DX12Device = createInfo.m_DX12Device;

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_CbvSrvUavCount;
			m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_RtvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Rtv)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_DsvCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Dsv)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);

			heapCreateInfo.m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
			heapCreateInfo.m_Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
			heapCreateInfo.m_DescriptorCount = createInfo.m_SamplerCount;
			m_Heaps[static_cast<uint8_t>(HeapType::Sampler)] = std::make_unique<DX12DescriptorHeap>(heapCreateInfo);
		}

		// Create Descriptor Allocators
		{
			const uint32_t totalSrvCount = createInfo.m_CbvSrvUavCount;
			const uint32_t developGuiSrvCount = createInfo.m_DevelopGuiSrvCount;
			const uint32_t bindlessSrvCount = createInfo.m_BindlessSrvCount;

			GGLAB_ASSERT_MSG(developGuiSrvCount + bindlessSrvCount < totalSrvCount,
				"Srv range overflow.");

			// DevelopGui Srv
			DX12DescriptorAllocatorBase::CreateInfo allocatorCreateInfo{};
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::CbvSrvUav)].get();
			allocatorCreateInfo.m_Range = { 0, developGuiSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::DevelopGuiSrv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// Bindless Srv
			allocatorCreateInfo.m_Range = { developGuiSrvCount, bindlessSrvCount };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::BindlessSrv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Srv
			allocatorCreateInfo.m_Range = { developGuiSrvCount + bindlessSrvCount,
				createInfo.m_CbvSrvUavCount - (developGuiSrvCount + bindlessSrvCount) };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::GeneralCbvSrvUav)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Rtv
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Rtv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_RtvCount };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::GeneralRtv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Dsv
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Dsv)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_DsvCount };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::GeneralDsv)] =
				std::make_unique<DX12DescriptorFreeListAllocator>(allocatorCreateInfo);

			// General Sampler
			allocatorCreateInfo.m_DescriptorHeap = m_Heaps[static_cast<uint8_t>(HeapType::Sampler)].get();
			allocatorCreateInfo.m_Range = { 0, createInfo.m_SamplerCount };
			m_FreeListAllocators[static_cast<uint8_t>(FreeListAllocatorType::GeneralSampler)] =
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
		GGLAB_ASSERT(static_cast<uint8_t>(heapType) < static_cast<uint8_t>(HeapType::Count));

		return m_Heaps[static_cast<uint8_t>(heapType)].get();
	}

	DX12DescriptorFreeListAllocator* DX12DescriptorManager::GetFreeListAllocator(FreeListAllocatorType allocatorType) const noexcept
	{
		GGLAB_ASSERT(static_cast<uint8_t>(allocatorType) < static_cast<uint8_t>(FreeListAllocatorType::Invalid));
		GGLAB_ASSERT(static_cast<uint8_t>(allocatorType) < static_cast<uint8_t>(FreeListAllocatorType::Count));

		return m_FreeListAllocators[static_cast<uint8_t>(allocatorType)].get();
	}

	DX12DescriptorID DX12DescriptorManager::AllocateBindlessDescriptorId() const noexcept
	{
		return GetFreeListAllocator(FreeListAllocatorType::BindlessSrv)->AllocateId();
	}

	void DX12DescriptorManager::RetireBindlessDescriptorId(const DX12DescriptorID& descriptorId, const DX12FencePoint& fencePoint) const noexcept
	{
		GetFreeListAllocator(FreeListAllocatorType::BindlessSrv)->RetireId(descriptorId, fencePoint);
	}
}