#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12Fence
	{
	public:
		explicit DX12Fence(DX12Device* device, uint64_t initValue = 1) noexcept;
		~DX12Fence();

		uint64_t GetCompletedValue() const noexcept;
		uint64_t GetCurrentValue() const noexcept { return m_CurrentValue; }
		bool IsCompleted(uint64_t fenceValue) const noexcept;
		DX12FencePoint Signal(DX12CommandQueue* dx12CommandQueue) noexcept;
		void WaitCompletion(uint64_t fenceValue, uint32_t timeout = GGLAB_INFINITE) const noexcept;

		ID3D12Fence* Get() const noexcept { return m_D3D12Fence.Get(); }

	private:
		ComPtr<ID3D12Fence> m_D3D12Fence;
		HANDLE m_EventHandle = nullptr;
		uint64_t m_CurrentValue = 0;
		uint64_t m_NextRequestValue = 0;
	};
}

