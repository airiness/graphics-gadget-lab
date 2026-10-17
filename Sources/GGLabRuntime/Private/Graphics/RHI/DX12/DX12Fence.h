#pragma once
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>
#include <limits>
#include <optional>

#include <d3d12.h>
#include <Windows.h>

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;

	// D3D12 reports this completed value for every fence of a removed device.
	inline constexpr uint64_t DX12DeviceRemovedFenceValue = std::numeric_limits<uint64_t>::max();

	enum class DX12FenceEventWait : uint8_t
	{
		Signaled,
		TimedOut,
		Failed,
	};

	// The removal sentinel counts as device loss only with native confirmation.
	[[nodiscard]] constexpr RHIFenceQueryResult ClassifyDX12FenceQuery(
		uint64_t completedValue, uint64_t targetValue, bool deviceRemoved) noexcept
	{
		if (targetValue == 0)
		{
			return RHIFenceQueryResult::Failed;
		}
		if (deviceRemoved)
		{
			return RHIFenceQueryResult::DeviceLost;
		}
		if (completedValue == DX12DeviceRemovedFenceValue)
		{
			return RHIFenceQueryResult::Failed;
		}
		return completedValue >= targetValue ?
			RHIFenceQueryResult::Completed :
			RHIFenceQueryResult::Pending;
	}

	// One step of a CPU fence wait, decided by the query that follows the event wait. A
	// signaled event alone proves nothing: device removal also signals fence events, and an
	// earlier registration on the shared event can wake the wait. nullopt waits again.
	[[nodiscard]] constexpr std::optional<RHIGpuWaitResult> ClassifyDX12FenceWaitStep(
		DX12FenceEventWait wait, RHIFenceQueryResult requery) noexcept
	{
		if (requery != RHIFenceQueryResult::Pending)
		{
			return ToRHIGpuWaitResult(requery);
		}
		switch (wait)
		{
		case DX12FenceEventWait::Signaled:
			return std::nullopt;
		case DX12FenceEventWait::TimedOut:
			return RHIGpuWaitResult::Timeout;
		case DX12FenceEventWait::Failed:
			break;
		}
		return RHIGpuWaitResult::Failed;
	}

	struct DX12SignalResult
	{
		// The requested point. It completes only once a native signal on the queue reached
		// it or the device was removed, so retirement tied to it is never shortened.
		DX12FencePoint m_FencePoint{};
		bool m_Signaled = false;

		// Checked signal: a fence point only when the native signal succeeded.
		[[nodiscard]] DX12FencePoint GetCheckedFencePoint() const noexcept
		{
			return m_Signaled ? m_FencePoint : DX12FencePoint{};
		}
	};

	class DX12Fence
	{
	public:
		explicit DX12Fence(DX12Device* device, uint64_t initValue = 1) noexcept;
		~DX12Fence();

		uint64_t GetCompletedValue() const noexcept;
		uint64_t GetCurrentValue() const noexcept { return m_CurrentValue; }
		RHIFenceHandle GetRHIHandle() const noexcept { return m_RHIHandle; }
		// Routine release query: true once the value was reached or the device was removed.
		bool IsCompleted(uint64_t fenceValue) const noexcept;
		[[nodiscard]] RHIFenceQueryResult Query(uint64_t fenceValue) const noexcept;
		DX12SignalResult Signal(DX12CommandQueue* dx12CommandQueue) noexcept;
		RHIGpuWaitResult WaitCompletion(uint64_t fenceValue,
			uint32_t timeoutMilliseconds = RHIInfiniteWaitMilliseconds) const noexcept;

		ID3D12Fence* Get() const noexcept { return m_D3D12Fence.Get(); }

	private:
		DX12Device* m_Device = nullptr;
		ComPtr<ID3D12Fence> m_D3D12Fence;
		HANDLE m_EventHandle = nullptr;
		RHIFenceHandle m_RHIHandle{};
		uint64_t m_CurrentValue = 0;
		uint64_t m_NextRequestValue = 0;
	};
}
