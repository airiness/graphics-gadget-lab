#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue
	{
	public:
		explicit DX12CommandQueue(
			DX12Device* dx12Device, 
			D3D12_COMMAND_LIST_TYPE type,
			int32_t priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL, 
			D3D12_COMMAND_QUEUE_FLAGS flags = D3D12_COMMAND_QUEUE_FLAG_NONE) noexcept;

		~DX12CommandQueue() noexcept = default;

		DX12CommandQueue(const DX12CommandQueue&) = delete;
		DX12CommandQueue& operator=(const DX12CommandQueue&) = delete;

		DX12CommandQueue(DX12CommandQueue&&) = delete;
		DX12CommandQueue& operator=(DX12CommandQueue&&) = delete;

		ID3D12CommandQueue* Get() const noexcept { return m_D3D12CommandQueue.Get(); }

	private:
		ComPtr<ID3D12CommandQueue> CreateCommandQueue(
			D3D12_COMMAND_LIST_TYPE type, 
			int32_t priority, 
			D3D12_COMMAND_QUEUE_FLAGS flags) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12CommandQueue> m_D3D12CommandQueue;
	};
}
