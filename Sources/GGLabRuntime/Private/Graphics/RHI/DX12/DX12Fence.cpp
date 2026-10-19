#include "Graphics/RHI/DX12/DX12Fence.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12HResult.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"

#include <atomic>
#include <cstdint>

namespace gglab
{
	namespace
	{
		RHIFenceHandle AllocateRHIFenceHandle() noexcept
		{
			static std::atomic<RHIFenceHandle::IndexType> nextIndex = 0;
			return RHIFenceHandle(nextIndex.fetch_add(1, std::memory_order_relaxed), 1);
		}
	}

	DX12Fence::DX12Fence(DX12Device* device, uint64_t initValue) noexcept :
		m_Device(device), m_RHIHandle(AllocateRHIFenceHandle()), m_NextRequestValue(initValue)
	{
		GGLAB_HR_DX(device->Get()->CreateFence(
			m_CurrentValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_D3D12Fence)),
			device->Get());
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
		return IsFenceSafeToRelease(Query(fenceValue));
	}

	RHIFenceQueryResult DX12Fence::Query(uint64_t fenceValue) const noexcept
	{
		const uint64_t completedValue = GetCompletedValue();
		const bool deviceRemoved =
			completedValue == DX12DeviceRemovedFenceValue && m_Device->IsDeviceRemoved();
		return m_Device->ObserveFenceQueryResult(
			ClassifyDX12FenceQuery(completedValue, fenceValue,
				deviceRemoved || m_Device->HasObservedDeviceLoss()));
	}

	DX12SignalResult DX12Fence::Signal(DX12CommandQueue* dx12CommandQueue) noexcept
	{
		// The value advances even when the signal fails: a later successful signal then also
		// completes this point, because a queue signals only after its earlier work. Only a
		// native removal is therefore sticky; callers handle the unsignaled point themselves.
		const uint64_t value = m_NextRequestValue++;
		const HRESULT result = dx12CommandQueue->Get()->Signal(Get(), value);
		const DX12SignalResult signal{ DX12FencePoint(this, value), SUCCEEDED(result) };
		if (signal.m_Signaled)
		{
			m_CurrentValue = value;
		}
		else
		{
			if (m_Device->IsDeviceRemoved())
			{
				m_Device->ObserveGpuWaitResult(RHIGpuWaitResult::DeviceLost);
			}
			GGLAB_LOG_GRAPHICS_ERROR("ID3D12CommandQueue::Signal failed (HRESULT 0x{:08X}).",
				static_cast<uint32_t>(result));
		}
		return signal;
	}

	RHIGpuWaitResult DX12Fence::WaitCompletion(
		uint64_t fenceValue, uint32_t timeoutMilliseconds) const noexcept
	{
		const ULONGLONG start = GetTickCount64();
		for (;;)
		{
			const RHIFenceQueryResult query = Query(fenceValue);
			if (query != RHIFenceQueryResult::Pending)
			{
				return m_Device->ObserveGpuWaitResult(ToRHIGpuWaitResult(query));
			}
			if (FAILED(m_D3D12Fence->SetEventOnCompletion(fenceValue, m_EventHandle)))
			{
				return m_Device->ObserveGpuWaitResult(m_Device->IsDeviceRemoved() ?
					RHIGpuWaitResult::DeviceLost : RHIGpuWaitResult::Failed);
			}
			DWORD remaining = INFINITE;
			if (timeoutMilliseconds != RHIInfiniteWaitMilliseconds)
			{
				const ULONGLONG elapsed = GetTickCount64() - start;
				remaining = elapsed >= timeoutMilliseconds ?
					0 :
					static_cast<DWORD>(timeoutMilliseconds - elapsed);
			}
			const DWORD waitResult = WaitForSingleObjectEx(m_EventHandle, remaining, FALSE);
			const DX12FenceEventWait wait = waitResult == WAIT_OBJECT_0 ?
				DX12FenceEventWait::Signaled :
				waitResult == WAIT_TIMEOUT ?
					DX12FenceEventWait::TimedOut :
					DX12FenceEventWait::Failed;
			if (const auto step = ClassifyDX12FenceWaitStep(wait, Query(fenceValue)))
			{
				return m_Device->ObserveGpuWaitResult(*step);
			}
		}
	}
}
