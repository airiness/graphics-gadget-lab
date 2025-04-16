#include "Precompiled.h"
#include "DX12Fence.h"
#include "DX12Device.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Fence::DX12Fence(DX12Device* device, uint64_t initValue) noexcept :
		m_DX12Device(device),
		m_CurrentValue(initValue)
	{
		utility::ThrowIfFailed(m_DX12Device->Get()->CreateFence(m_CurrentValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3D12Fence)));

		m_EventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}

	DX12Fence::~DX12Fence() noexcept
	{
		if (m_EventHandle)
		{
			CloseHandle(m_EventHandle);
			m_EventHandle = nullptr;
		}
	}

	uint64_t DX12Fence::GetCurrentValue() const noexcept
	{
		return m_D3D12Fence->GetCompletedValue();
	}

	void DX12Fence::Signal(DX12CommandQueue* dx12CommandQueue, uint64_t value) noexcept
	{
	}

	void DX12Fence::WaitForValue(uint64_t value, uint32_t timeout) noexcept
	{
	}

}
