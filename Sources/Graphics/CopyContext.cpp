#include "Core/Precompiled.h"
#include "Graphics/CopyContext.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"

namespace gglab
{
	CopyContext::CopyContext(DX12Device* dx12Device) noexcept :
		m_DX12Device(dx12Device)
	{
		GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device pointer can not be null.");

		m_CommandQueue = dx12Device->GetCommandQueue(CommandQueueType::Transfer);
		m_CommandAllocatorPool = dx12Device->GetCommandAllocatorPool(CommandQueueType::Transfer);
		GGLAB_ASSERT_MSG(m_CommandQueue != nullptr && m_CommandAllocatorPool != nullptr,
			"Copy Command Queue or Command Allocator Pool is null.");

		DX12CommandList::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_DX12Device;
		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_CommandList = std::make_unique<DX12CommandList>(createInfo);
	}

	void CopyContext::Begin() noexcept
	{
		// Do this in TransferManager, not here.
		//ReclaimCompleted();

		GGLAB_ASSERT_MSG(!m_ExecutingInfo,
			"CopyContext::Begin() called while a copy operation is already in progress.");
		GGLAB_ASSERT_MSG(m_CurrentCommandAllocator == nullptr,
			"CopyContext::Begin() found a non-null current command allocator.");

		m_ExecutingInfo = std::make_unique<InFlightInfo>();
		m_CurrentCommandAllocator = m_CommandAllocatorPool->RequestCommandAllocator();
		m_CommandList->Begin(m_CurrentCommandAllocator);
	}

	DX12FencePoint CopyContext::End(bool wait) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo,
			"CopyContext::Execute() called without a matching Begin().");

		m_CommandList->FlushBarriers();
		m_CommandList->End();

		DX12CommandList* commandLists[] = { m_CommandList.get() };
		DX12FencePoint fencePoint = m_CommandQueue->Execute(std::span{ commandLists });

		m_CommandAllocatorPool->RecycleCommandAllocator(m_CurrentCommandAllocator, fencePoint);
		m_CurrentCommandAllocator = nullptr;

		if (wait)
		{
			fencePoint.Wait();
		}

		m_ExecutingInfo->m_FencePoint = fencePoint;
		m_InFlightInfos.push_back(std::move(m_ExecutingInfo));

		return fencePoint;
	}

	DX12FencePoint CopyContext::ReclaimCompleted() noexcept
	{
		DX12FencePoint doneFencePoint;
		auto iter = std::remove_if(m_InFlightInfos.begin(), m_InFlightInfos.end(),
			[&doneFencePoint](const std::unique_ptr<InFlightInfo>& info)
			{
				if (info->m_FencePoint.IsCompleted())
				{
					if (!doneFencePoint.IsValid() || info->m_FencePoint.GetValue() > doneFencePoint.GetValue())
					{
						doneFencePoint = info->m_FencePoint;
						return true;
					}
				}
				return false;
			});

		m_InFlightInfos.erase(iter, m_InFlightInfos.end());

		return doneFencePoint;
	}

	void CopyContext::CopyBuffer(DX12Buffer* dstBuffer, uint64_t dstOffset,
		DX12Buffer* srcBuffer, uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "CopyBuffer must be called between Begin() and End().");

		auto* commandList = m_CommandList->Get();
		GGLAB_ASSERT(commandList && dstBuffer && srcBuffer && numBytes);
		commandList->CopyBufferRegion(dstBuffer->Get(), dstOffset,
			srcBuffer->Get(), srcOffset,
			numBytes);
	}

	void CopyContext::UploadResource(const void* data, uint64_t sizeInBytes,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");

		auto uploadBuffer = std::make_unique<DX12Buffer>();
		uploadBuffer->Create(DX12Buffer::UploadBufferCreateInfo(m_DX12Device->GetMemAllocator(), sizeInBytes));
		uploadBuffer->SetDebugName(L"CopyContext.UploadIntermediateBuffer");

		D3D12_SUBRESOURCE_DATA subResourceData = {};
		subResourceData.pData = data;
		subResourceData.RowPitch = static_cast<LONG_PTR>(sizeInBytes);
		subResourceData.SlicePitch = static_cast<LONG_PTR>(sizeInBytes);

		UpdateSubresources<1>(m_CommandList->Get(),
			dstResource->Get(),
			uploadBuffer->Get(),
			0, 0, 1, &subResourceData);

		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
	}

	void CopyContext::UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");

		auto subResourceCount = static_cast<UINT>(subResources.size());
		auto uploadSize = GetRequiredIntermediateSize(dstResource->Get(), 0, subResourceCount);

		auto uploadBuffer = std::make_unique<DX12Buffer>();
		uploadBuffer->Create(DX12Buffer::UploadBufferCreateInfo(m_DX12Device->GetMemAllocator(), static_cast<uint64_t>(uploadSize)));
		uploadBuffer->SetDebugName(L"CopyContext.UploadIntermediateBuffer");

		UpdateSubresources(m_CommandList->Get(),
			dstResource->Get(),
			uploadBuffer->Get(),
			0, 0, subResourceCount, subResources.data());

		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
	}

	void CopyContext::UploadResource(
		const RHITextureUploadData& uploadData,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");

		std::vector<D3D12_SUBRESOURCE_DATA> nativeSubresources;
		nativeSubresources.reserve(uploadData.m_Subresources.size());
		for (const RHITextureSubresourceData& subresource : uploadData.m_Subresources)
		{
			if (!subresource.m_Data || subresource.m_RowPitch == 0 || subresource.m_SlicePitch == 0)
			{
				GGLAB_LOG_GRAPHICS_WARN("CopyContext::UploadResource skipped an invalid texture subresource.");
				continue;
			}

			nativeSubresources.push_back(
				{
					.pData = subresource.m_Data,
					.RowPitch = static_cast<LONG_PTR>(subresource.m_RowPitch),
					.SlicePitch = static_cast<LONG_PTR>(subresource.m_SlicePitch),
				});
		}

		if (nativeSubresources.empty())
		{
			GGLAB_LOG_GRAPHICS_ERROR("CopyContext::UploadResource received no valid texture subresources.");
			return;
		}

		UploadResource(nativeSubresources, dstResource);
	}
}
