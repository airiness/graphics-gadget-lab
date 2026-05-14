#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12Resource.h"
#include "Core/HResult.h"

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

		GGLAB_HR(m_Allocator->CreateResource(
			&createInfo.m_AllocDesc,
			&m_ResourceDesc,
			createInfo.m_InitStates,
			m_ClearValue.has_value() ? &m_ClearValue.value() : nullptr,
			&m_Allocation,
			IID_PPV_ARGS(&m_Resource)));

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

	bool DX12Resource::TransitionNeeded(D3D12_RESOURCE_STATES newStates) const noexcept
	{
		// TODO: per subresource?
		return m_ResourceState != newStates;
	}

	void DX12Resource::AdoptExternal(ComPtr<ID3D12Resource> resource, D3D12_RESOURCE_STATES initStates) noexcept
	{
		// Reset old Allocation
		m_Allocation.Reset();
		m_Allocator = nullptr;
		m_Resource = std::move(resource);
		m_ResourceDesc = CD3DX12_RESOURCE_DESC(m_Resource->GetDesc());
		m_ResourceState = initStates;
		m_ClearValue.reset();
	}

	void DX12Resource::AliasTo(const AliasingInfo& aliasingInfo) noexcept
	{
		m_Resource.Reset();
		m_Allocation = aliasingInfo.m_Allocation;

		D3D12MA::Allocator* allocator = aliasingInfo.m_Allocator ? aliasingInfo.m_Allocator : m_Allocator;
		GGLAB_HR(allocator->CreateAliasingResource(m_Allocation.Get(),
			aliasingInfo.m_LocalOffset,
			&aliasingInfo.m_ResourceDesc,
			aliasingInfo.m_InitStates,
			aliasingInfo.m_ClearValue.has_value() ? &aliasingInfo.m_ClearValue.value() : nullptr,
			IID_PPV_ARGS(&m_Resource)));

		m_ResourceDesc = aliasingInfo.m_ResourceDesc;
		m_ResourceState = aliasingInfo.m_InitStates;
		m_ClearValue = aliasingInfo.m_ClearValue;
	}

	void DX12Resource::Release() noexcept
	{
		m_Resource.Reset();
		m_Allocation.Reset();
		m_ClearValue.reset();
		m_ResourceDesc = {};
		m_ResourceState = D3D12_RESOURCE_STATE_COMMON;
	}

	D3D12_RESOURCE_BARRIER DX12Resource::MakeTransition(D3D12_RESOURCE_STATES newState, uint32_t subResource) const noexcept
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Transition.pResource = m_Resource.Get();
		barrier.Transition.Subresource = static_cast<UINT>(subResource);
		barrier.Transition.StateBefore = m_ResourceState;
		barrier.Transition.StateAfter = newState;

		return barrier;
	}

	void DX12Resource::SetDebugName(const wchar_t* name) noexcept
	{
#if defined (BUILD_DEBUG)
		if (m_Resource)
		{
			//utility::SetDebugName(m_Resource.Get(), name); // TODO: Process Debug Name Settings
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

	bool DX12Resource::HasClearValue() const noexcept
	{
		return m_ClearValue.has_value();
	}

	const D3D12_CLEAR_VALUE* DX12Resource::GetClearValue() const noexcept
	{
		if (m_ClearValue.has_value())
		{
			return &m_ClearValue.value();
		}
		
		return nullptr;
	}

	D3D12_RESOURCE_BARRIER DX12Resource::MakeAliasingBarrier(const DX12Resource* before, const DX12Resource* after) noexcept
	{
		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		barrier.Aliasing.pResourceBefore = before ? before->Get() : nullptr;
		barrier.Aliasing.pResourceAfter = after ? after->Get() : nullptr;

		return barrier;
	}
}