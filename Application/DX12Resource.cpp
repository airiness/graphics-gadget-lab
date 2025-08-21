#include "Precompiled.h"
#include "DX12Resource.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Resource::DX12Resource(const CreateInfo& createInfo) noexcept :
		m_ResourceDesc(createInfo.m_ResourceDesc),
		m_ResourceState(createInfo.m_InitStates),
		m_ClearValue(createInfo.m_ClearValue)
	{
		utility::ThrowIfFailed(createInfo.m_Allocator->CreateResource(
			&createInfo.m_AllocDesc,
			&createInfo.m_ResourceDesc,
			createInfo.m_InitStates,
			createInfo.m_ClearValue.has_value() ? &createInfo.m_ClearValue.value() : nullptr,
			&m_Allocation,
			IID_PPV_ARGS(&m_Resource)));
	}

	ID3D12Resource* DX12Resource::Get() const
	{
		return m_Resource.Get();
	}

	bool DX12Resource::TransitionNeeded(D3D12_RESOURCE_STATES newStates) const noexcept
	{
		return m_ResourceState != newStates;
	}

	void DX12Resource::AdoptExternal(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES initStates) noexcept
	{
		// Reset old Allocation
		m_Allocation.Reset();
		m_Resource = resource;
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(resource->GetDesc());
		m_ResourceState = initStates;
		m_ClearValue.reset();
	}

	void DX12Resource::RebindAliasing(const AliasingInfo& aliasingInfo) noexcept
	{
		
	}

	void DX12Resource::Reset() noexcept
	{
	}

	void DX12Resource::SetDebugName(const wchar_t* name) noexcept
	{
#if defined (BUILD_DEBUG)
		if (m_Resource)
		{
			utility::SetDebugName(m_Resource.Get(), name);
		}

		if (m_Allocation)
		{
			m_Allocation->SetName(name);
		}
#endif
	}
}


