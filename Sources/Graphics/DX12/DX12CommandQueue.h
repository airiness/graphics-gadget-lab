#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/DX12/DX12FencePoint.h"

namespace gglab
{
	class DX12Device;
	class DX12Fence;
	class DX12CommandList;
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

		DX12FencePoint Execute(std::span<const DX12CommandList* const> commandLists) noexcept;

		void FlushCommandQueue() noexcept;

		DX12FencePoint Signal() noexcept;

		void Wait(const DX12FencePoint& fencePoint) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12CommandQueue> m_D3D12CommandQueue;

		std::unique_ptr<DX12Fence> m_Fence;
	};
}
