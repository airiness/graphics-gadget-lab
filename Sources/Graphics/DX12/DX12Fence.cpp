#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12Fence.h"
#include "Graphics/DX12/DX12Device.h"
#include "Graphics/DX12/DX12CommandQueue.h"
#include "Core/HResult.h"

namespace gglab
{
	DX12Fence::DX12Fence(DX12Device* device, uint64_t initValue) noexcept :
		m_NextRequestValue(initValue)
	{
		GGLAB_HR_DX(device->Get()->CreateFence(m_CurrentValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3D12Fence)), device->Get());
		m_EventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}

	DX12Fence::~DX12Fence()
	{
		if (m_EventHandle)
		{
			CloseHandle(m_EventHandle);
			m_EventHandle = nullptr;
		}
	}

	uint64_t DX12Fence::GetCompletedValue() const noexcept
	{
		return m_D3D12Fence->GetCompletedValue();
	}

	bool DX12Fence::IsCompleted(uint64_t fenceValue) const noexcept
	{
		const auto completedValue = GetCompletedValue();
		//OutputDebugString(std::format(L"Completed fence value: {}, Checked fence value: {}\n", completedValue, fenceValue).c_str());
		return completedValue >= fenceValue;
	}

	DX12FencePoint DX12Fence::Signal(DX12CommandQueue* dx12CommandQueue) noexcept
	{
		dx12CommandQueue->Get()->Signal(Get(), m_NextRequestValue);

		DX12FencePoint fencePoint(this, m_NextRequestValue);

		m_CurrentValue = m_NextRequestValue;
		++m_NextRequestValue;

		return fencePoint;
	}

	void DX12Fence::WaitCompletion(uint64_t fenceValue, uint32_t timeout) const noexcept
	{
		if (!IsCompleted(fenceValue))
		{
			m_D3D12Fence->SetEventOnCompletion(fenceValue, m_EventHandle);
			WaitForSingleObjectEx(m_EventHandle, timeout, FALSE);
		}
	}
}
