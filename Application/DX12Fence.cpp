#include "Precompiled.h"
#include "DX12Fence.h"
#include "DX12Device.h"
#include "DX12CommandQueue.h"
#include "Utility.h"

namespace graphicsGadgetLab
{
	DX12Fence::DX12Fence(DX12Device* device, uint64_t initValue) noexcept :
		m_CurrentValue(initValue)
	{
		utility::ThrowIfFailed(device->Get()->CreateFence(m_CurrentValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3D12Fence)));
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

	bool DX12Fence::IsCompleted(uint64_t value) const noexcept
	{
		return m_D3D12Fence->GetCompletedValue() >= value;
	}

	void DX12Fence::Signal(DX12CommandQueue* dx12CommandQueue, uint64_t value) noexcept
	{
		if (!IsCompleted(value))
		{
			m_D3D12Fence->Signal(value);
		}
	}

	void DX12Fence::WaitForValue(uint64_t value, uint32_t timeout) noexcept
	{
		if (!IsCompleted(value))
		{
			m_D3D12Fence->SetEventOnCompletion(value, m_EventHandle);
			WaitForSingleObjectEx(m_EventHandle, timeout, FALSE);
		}
	}

}
