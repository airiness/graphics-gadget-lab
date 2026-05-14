#pragma once
#include "Graphics/DX12/DX12FencePoint.h"
#include "Graphics/DX12/DX12CommandList.h"

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12Resource;
	class DX12CommandQueue;
	class DX12CommandAllocator;
	class DX12CommandAllocatorPool;

	class CopyContext final
	{
	public:
		CopyContext(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(CopyContext);
		~CopyContext() = default;

		void Begin() noexcept;
		DX12FencePoint End(bool wait = false) noexcept;
		DX12FencePoint ReclaimCompleted() noexcept;

		void CopyBuffer(DX12Buffer* dst, uint64_t dstOffset,
			DX12Buffer* src, uint64_t srcOffset,
			uint64_t numBytes) noexcept;
		void UploadResource(const void* data, uint64_t sizeInBytes, 
			const DX12Resource* dstResource) noexcept;
		void UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
			const DX12Resource* dstResource) noexcept;

		DX12CommandList* GetCommandList() noexcept { return m_CommandList.get(); }
		const DX12CommandList* GetCommandList() const noexcept { return m_CommandList.get(); }

	private:
		struct InFlightInfo
		{
			DX12FencePoint m_FencePoint;
			std::vector<std::unique_ptr<DX12Buffer>> m_IntermediateBuffers;
		};

		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_CommandQueue = nullptr;
		DX12CommandAllocatorPool* m_CommandAllocatorPool = nullptr;
		DX12CommandAllocator* m_CurrentCommandAllocator = nullptr;
		std::unique_ptr<DX12CommandList> m_CommandList = nullptr;

		std::unique_ptr<InFlightInfo> m_ExecutingInfo = nullptr;
		std::vector<std::unique_ptr<InFlightInfo>> m_InFlightInfos;
	};
}