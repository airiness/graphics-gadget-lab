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


