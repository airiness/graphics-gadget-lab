#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12Fence
	{
	public:
		explicit DX12Fence(DX12Device* device, uint64_t initValue = 0) noexcept;
		~DX12Fence() noexcept;

		uint64_t GetCurrentValue() const noexcept;
		bool IsCompleted(uint64_t value) const noexcept;
		void Signal(DX12CommandQueue* dx12CommandQueue, uint64_t value) noexcept;
		void WaitForValue(uint64_t value, uint32_t timeout = INFINITE) noexcept;

		ID3D12Fence* Get() const noexcept { return m_D3D12Fence.Get(); }

	private:
		ComPtr<ID3D12Fence> m_D3D12Fence;
		HANDLE m_EventHandle = nullptr;
		uint64_t m_CurrentValue = 0;
	};
}

