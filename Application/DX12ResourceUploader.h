#pragma once
#include "DX12FencePoint.h"

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12CommandList;
	class DX12CommandAllocator;
	class DX12CommandAllocatorPool;
	class DX12Resource;
	class DX12Buffer;
	class DX12ResourceUploader final
	{
	public:
		explicit DX12ResourceUploader(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12ResourceUploader);
		~DX12ResourceUploader() noexcept;

		void Finalize() noexcept;

		void BeginUpload() noexcept;
		void EndUpload(bool wait) noexcept;

		void UploadResource(const void* data, uint64_t dataSize, const DX12Resource* destResource) noexcept;
		void UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResourceData, const DX12Resource* destResource) noexcept;

		DX12CommandList* GetUploadCommandList() const { return m_UploadCommandList.get(); }

	private:
		DX12Device* m_DX12Device = nullptr;
		std::unique_ptr<DX12CommandQueue> m_UploadCommandQueue = nullptr;
		std::unique_ptr<DX12CommandList> m_UploadCommandList = nullptr;
		std::unique_ptr<DX12CommandAllocatorPool> m_UploadCommandAllocatorPool = nullptr;

		struct UploadTracedInfo
		{
			DX12FencePoint m_FencePoint;
			DX12CommandAllocator* m_CommandAllocator = nullptr;
			std::vector<std::unique_ptr<DX12Buffer>> m_UploadIntermediateResources;
		};

		std::unique_ptr<UploadTracedInfo> m_UploadingInfo = nullptr;
		std::vector<std::unique_ptr<UploadTracedInfo>> m_UploadTracedInfos;
	};
}