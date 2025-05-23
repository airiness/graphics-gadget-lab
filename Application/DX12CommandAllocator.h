#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandAllocator
	{
	public:
		explicit DX12CommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;
		~DX12CommandAllocator() noexcept = default;

		ID3D12CommandAllocator* Get() const noexcept { return m_D3D12CommandAllocator.Get(); };

		void SetFenceValue(uint64_t value) noexcept { m_FenceValue = value; }
		uint64_t GetFenceValue() const noexcept { return m_FenceValue; }

		void SetInUse(bool inUse) noexcept { m_InUse = inUse; }
		bool IsInUse() const noexcept { return m_InUse; }

	private:
		void CreateCommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;
	private:
		ComPtr<ID3D12CommandAllocator> m_D3D12CommandAllocator;
		uint64_t m_FenceValue = 0;
		bool m_InUse = false;
	};

	class DX12CommandAllocatorPool final
	{
	public:
		explicit DX12CommandAllocatorPool(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;
		~DX12CommandAllocatorPool() noexcept;

		DX12CommandAllocator* RequestCommandAllocator(uint64_t fenceValue) noexcept;
		void RecycleCommandAllocator(DX12CommandAllocator* allocator, uint64_t fenceValue) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_COMMAND_LIST_TYPE m_Type;

		std::vector<std::unique_ptr<DX12CommandAllocator>> m_Pool;	
		std::queue<DX12CommandAllocator*> m_AvailableAllocators;

		std::mutex m_Mutex;
	};
}