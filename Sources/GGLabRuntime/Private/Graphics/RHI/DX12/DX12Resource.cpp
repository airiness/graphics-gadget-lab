#include "Graphics/RHI/DX12/DX12Resource.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/HResult.h"

#include <atomic>
#include <utility>

namespace gglab
{
	namespace
	{
		std::atomic<uint64_t> g_NextNativeAllocationId{ 1 };

		RHIMemoryClass ToRHIMemoryClass(const D3D12_HEAP_PROPERTIES& heap) noexcept
		{
			const bool local = heap.Type == D3D12_HEAP_TYPE_DEFAULT ||
				(heap.Type == D3D12_HEAP_TYPE_CUSTOM &&
					heap.MemoryPoolPreference == D3D12_MEMORY_POOL_L1);
			return local ? RHIMemoryClass::DeviceLocal : RHIMemoryClass::Other;
		}
	}

	void DX12Resource::Create(const CreateInfo& createInfo) noexcept
	{
		if (IsValid())
		{
			Release();
		}

		m_ResourceDesc = createInfo.m_ResourceDesc;
		m_ResourceState = createInfo.m_InitStates;
		m_ClearValue = createInfo.m_ClearValue;
		m_Allocator = createInfo.m_Allocator;

		GGLAB_ASSERT_MSG(m_Allocator, "Allocator can not be null.");

		if (createInfo.m_EnhancedInitialLayout)
		{
			const CD3DX12_RESOURCE_DESC1 resourceDesc1(m_ResourceDesc);
			GGLAB_HR(m_Allocator->CreateResource3(&createInfo.m_AllocDesc, &resourceDesc1,
				*createInfo.m_EnhancedInitialLayout,
				m_ClearValue.has_value() ? &m_ClearValue.value() : nullptr, 0, nullptr,
				&m_Allocation, IID_PPV_ARGS(&m_Resource)));
		}
		else
		{
			GGLAB_HR(m_Allocator->CreateResource(&createInfo.m_AllocDesc, &m_ResourceDesc,
				createInfo.m_InitStates, m_ClearValue.has_value() ? &m_ClearValue.value() : nullptr,
				&m_Allocation, IID_PPV_ARGS(&m_Resource)));
		}
		// Each created resource owns its allocation, so the identity is assigned with it.
		m_NativeAllocationId = m_Allocation ?
			g_NextNativeAllocationId.fetch_add(1, std::memory_order_relaxed) :
			0;
	}

	ID3D12Resource* DX12Resource::Get() const noexcept
	{
		return m_Resource.Get();
	}

	D3D12_RESOURCE_DESC DX12Resource::GetDesc() const noexcept
	{
		return m_Resource->GetDesc();
	}

	D3D12_RESOURCE_STATES DX12Resource::GetState() const noexcept
	{
		return m_ResourceState;
	}

	void DX12Resource::AdoptExternal(
		ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES initStates) noexcept
	{
		// Reset old Allocation
		m_Allocation.Reset();
		m_Allocator = nullptr;
		m_NativeAllocationId = 0;
		m_Resource = std::move(resource);
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(m_Resource->GetDesc());
		m_ResourceState = initStates;
		m_ClearValue.reset();
	}

	void DX12Resource::Release() noexcept
	{
		m_Resource.Reset();
		m_Allocation.Reset();
		m_NativeAllocationId = 0;
		m_ClearValue.reset();
		m_ResourceDesc = {};
		m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	void DX12Resource::SetDebugName(const wchar_t* name) noexcept
	{
#if GGLAB_DEVELOPMENT_BUILD
		if (m_Resource)
		{
			m_Resource->SetName(name);
		}

		if (m_Allocation)
		{
			m_Allocation->SetName(name);
		}
#endif
	}

	bool DX12Resource::IsValid() const noexcept
	{
		return m_Resource != nullptr;
	}

	bool DX12Resource::IsExternal() const noexcept
	{
		return IsValid() && !OwnsAllocation();
	}

	bool DX12Resource::OwnsAllocation() const noexcept
	{
		return m_Allocation != nullptr;
	}

	RHIAllocationFacts DX12Resource::GetAllocationFacts() const noexcept
	{
		if (!IsValid())
		{
			return { .m_Status = RHIAllocationFactsStatus::Unowned };
		}
		if (!OwnsAllocation())
		{
			return { .m_Status = RHIAllocationFactsStatus::External };
		}
		D3D12_HEAP_PROPERTIES heap{};
		const bool heapKnown = SUCCEEDED(m_Resource->GetHeapProperties(&heap, nullptr));
		// D3D12MA records only the requested alignment for committed allocations, which is zero
		// for the default. The driver reports the placement alignment the resource requires.
		uint64_t alignment = m_Allocation->GetAlignment();
		ComPtr<ID3D12Device> device;
		if (SUCCEEDED(m_Resource->GetDevice(IID_PPV_ARGS(&device))))
		{
			const D3D12_RESOURCE_DESC desc = m_Resource->GetDesc();
			alignment = device->GetResourceAllocationInfo(0, 1, &desc).Alignment;
		}
		return {
			.m_Status = RHIAllocationFactsStatus::Known,
			.m_AllocatedBytes = m_Allocation->GetSize(),
			.m_AlignmentBytes = alignment,
			.m_MemoryClass = heapKnown ? ToRHIMemoryClass(heap) : RHIMemoryClass::Unknown,
			.m_OffsetBytes = 0,
			.m_NativeAllocationId = m_NativeAllocationId,
		};
	}

	const D3D12_CLEAR_VALUE* DX12Resource::GetClearValue() const noexcept
	{
		if (m_ClearValue.has_value())
		{
			return &m_ClearValue.value();
		}

		return nullptr;
	}
}
