#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>
#include <memory>
#include <span>

#include <d3d12.h>

namespace gglab
{
	class DX12Device;
	class DX12Fence;
	class DX12CommandList;
	struct DX12SignalResult;
	class DX12CommandQueue
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
			int32_t m_Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
			D3D12_COMMAND_QUEUE_FLAGS m_Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		};

	public:
		explicit DX12CommandQueue(const CreateInfo& crateInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12CommandQueue);
		~DX12CommandQueue();

		ID3D12CommandQueue* Get() const noexcept { return m_D3D12CommandQueue.Get(); }
		const DX12Fence* GetFence() const noexcept { return m_Fence.get(); }

		// Retirement bookkeeping: the returned point completes only after a native signal on
		// this queue reached it or the device was removed, even when this signal failed.
		DX12FencePoint Execute(std::span<const DX12CommandList* const> commandLists) noexcept;
		RHIFencePoint Submit(std::span<const DX12CommandList* const> commandLists) noexcept;
		// Frame transactions classify the submission from the checked signal result.
		DX12SignalResult ExecuteChecked(
			std::span<const DX12CommandList* const> commandLists) noexcept;

		// Signals a new value and waits for it; Completed means this queue is idle.
		RHIGpuWaitResult FlushCommandQueue(
			uint32_t timeoutMilliseconds = RHIInfiniteWaitMilliseconds) noexcept;

		DX12FencePoint Signal() noexcept;
		DX12SignalResult SignalChecked() noexcept;

		// GPU-side wait. False when the native wait failed; later work is then not ordered
		// after the fence.
		[[nodiscard]] bool Wait(const DX12FencePoint& fencePoint) const noexcept;
		[[nodiscard]] bool Wait(const DX12Fence& fence, uint64_t value) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12CommandQueue> m_D3D12CommandQueue;

		std::unique_ptr<DX12Fence> m_Fence;
	};
}
