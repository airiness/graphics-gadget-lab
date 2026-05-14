#pragma once
#include "Graphics/DX12/DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandAllocator
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		};

	public:
		explicit DX12CommandAllocator(const CreateInfo& createInfo) noexcept;
		~DX12CommandAllocator() = default;

		ID3D12CommandAllocator* Get() const noexcept { return m_D3D12CommandAllocator.Get(); };

	private:
		ComPtr<ID3D12CommandAllocator> m_D3D12CommandAllocator;
	};

	class DX12CommandAllocatorPool final
	{
	public:
		explicit DX12CommandAllocatorPool(const DX12CommandAllocator::CreateInfo& createInfo) noexcept;
		~DX12CommandAllocatorPool() = default;

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