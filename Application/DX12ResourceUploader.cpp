#include "Precompiled.h"
#include "DX12ResourceUploader.h"
#include "DX12CommandQueue.h"
#include "DX12CommandList.h"
#include "DX12CommandAllocator.h"
#include "DX12Fence.h"
#include "DX12Buffer.h"

namespace graphicsGadgetLab
{
	DX12ResourceUploader::DX12ResourceUploader(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device),
		m_UploadCommandQueue(std::make_unique<DX12CommandQueue>(m_DX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT)),
		m_UploadCommandList(std::make_unique<DX12CommandList>(m_DX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT)),
		m_UploadCommandAllocatorPool(std::make_unique<DX12CommandAllocatorPool>(m_DX12Device, D3D12_COMMAND_LIST_TYPE_DIRECT))
	{
	}

	DX12ResourceUploader::~DX12ResourceUploader() noexcept
	{
	}

	void DX12ResourceUploader::Finalize() noexcept
	{
	}

	void DX12ResourceUploader::BeginUpload() noexcept
	{
		// clear completed upload traced infos
		std::erase_if(m_UploadTracedInfos,
			[](const std::unique_ptr<UploadTracedInfo>& info)
			{
				GGLAB_ASSERT_MSG(info->m_FencePoint.IsValid(), "UploadTracedInfo::m_FencePoint is not valid.");
				return info->m_FencePoint.IsCompleted();
			});

		GGLAB_ASSERT_MSG(m_UploadingInfo == nullptr, "DX12ResourceUploader::BeginUpload() called while an upload is already in progress.");

		m_UploadingInfo = std::make_unique<UploadTracedInfo>();
		m_UploadingInfo->m_CommandAllocator = m_UploadCommandAllocatorPool->RequestCommandAllocator();

		m_UploadCommandList->Begin(m_UploadingInfo->m_CommandAllocator);
	}

	void DX12ResourceUploader::EndUpload(bool wait) noexcept
	{
		m_UploadCommandList->FlushBarriers();
		m_UploadCommandList->End();
		DX12CommandList* const commandLists[] = { m_UploadCommandList.get() };

		m_UploadingInfo->m_FencePoint = m_UploadCommandQueue->Execute(std::span{ commandLists });

		m_UploadCommandAllocatorPool->RecycleCommandAllocator(m_UploadingInfo->m_CommandAllocator, m_UploadingInfo->m_FencePoint);

		if (wait)
		{
			m_UploadingInfo->m_FencePoint.Wait();
		}

		m_UploadTracedInfos.push_back(std::move(m_UploadingInfo));
		m_UploadingInfo = nullptr;
	}

	void DX12ResourceUploader::UploadResource(const void* data, size_t dataSize, const DX12Resource* destResource) noexcept
	{
		std::unique_ptr<DX12Buffer> uploadBuffer = std::make_unique<DX12Buffer>(m_DX12Device,
			D3D12_HEAP_TYPE_UPLOAD,
			CD3DX12_RESOURCE_DESC::Buffer(dataSize),
			D3D12_RESOURCE_STATE_GENERIC_READ);
		uploadBuffer->SetDebugName(L"UploadIntermediateBuffer");

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data;
		subResourceData.RowPitch = static_cast<LONG_PTR>(dataSize);
		subResourceData.SlicePitch = static_cast<LONG_PTR>(dataSize);

		UpdateSubresources<1>(m_UploadCommandList->Get(),
			destResource->Get(),
			uploadBuffer->Get(),
			0, 0, 1, &subResourceData);

		m_UploadingInfo->m_UploadIntermediateResources.push_back(std::move(uploadBuffer));
	}

	void DX12ResourceUploader::UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResourceData, const DX12Resource* destResource) noexcept
	{
		auto subResourceCount = static_cast<UINT>(subResourceData.size());
		auto uploadSize = GetRequiredIntermediateSize(destResource->Get(), 0, subResourceCount);

		std::unique_ptr uploadBuffer = std::make_unique<DX12Buffer>(m_DX12Device,
			D3D12_HEAP_TYPE_UPLOAD,
			CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
			D3D12_RESOURCE_STATE_GENERIC_READ);
		uploadBuffer->SetDebugName(L"UploadIntermediateBuffer");

		UpdateSubresources(m_UploadCommandList->Get(),
			destResource->Get(),
			uploadBuffer->Get(),
			0, 0, subResourceCount, subResourceData.data());

		m_UploadingInfo->m_UploadIntermediateResources.push_back(std::move(uploadBuffer));
	}
}
