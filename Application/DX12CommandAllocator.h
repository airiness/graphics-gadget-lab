#pragma once
#include "DX12FencePoint.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandAllocator
	{
	public:
		explicit DX12CommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;
		~DX12CommandAllocator() noexcept = default;

		ID3D12CommandAllocator* Get() const noexcept { return m_D3D12CommandAllocator.Get(); };

	private:
		void CreateCommandAllocator(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;

	private:
		ComPtr<ID3D12CommandAllocator> m_D3D12CommandAllocator;
	};

	class DX12CommandAllocatorPool final
	{
	public:
		explicit DX12CommandAllocatorPool(DX12Device* dx12Device, D3D12_COMMAND_LIST_TYPE type) noexcept;
		~DX12CommandAllocatorPool() noexcept;

		DX12CommandAllocator* RequestCommandAllocator() noexcept;
		void RecycleCommandAllocator(DX12CommandAllocator* allocator, DX12FencePoint fencePoint) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		D3D12_COMMAND_LIST_TYPE m_Type;

		std::vector<std::unique_ptr<DX12CommandAllocator>> m_Pool;	
		std::queue<std::pair<DX12CommandAllocator*, DX12FencePoint>> mRecycledAllocators;

		std::mutex m_Mutex;
	};
}