#include "Graphics/RHI/DX12/DX12Resource.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/HResult.h"

#include <utility>

namespace gglab
{
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
		m_Resource = std::move(resource);
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(m_Resource->GetDesc());
		m_ResourceState = initStates;
		m_ClearValue.reset();
	}

	void DX12Resource::Release() noexcept
	{
		m_Resource.Reset();
		m_Allocation.Reset();
		m_ClearValue.reset();
		m_ResourceDesc = {};
		m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	void DX12Resource::SetDebugName(const wchar_t* name) noexcept
	{
#if defined(BUILD_DEBUG)
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

	const D3D12_CLEAR_VALUE* DX12Resource::GetClearValue() const noexcept
	{
		if (m_ClearValue.has_value())
		{
			return &m_ClearValue.value();
		}

		return nullptr;
	}
}
