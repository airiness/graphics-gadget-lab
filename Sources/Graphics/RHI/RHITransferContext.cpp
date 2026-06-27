#include "Core/Precompiled.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"

#include <cstring>

namespace gglab
{
	RHITransferContext::RHITransferContext(DX12Device* dx12Device) noexcept :
		m_Handle(AllocateDX12CommandContextHandle()),
		m_DX12Device(dx12Device)
	{
		GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device pointer can not be null.");

		m_CommandQueue = dx12Device->GetCommandQueue(CommandQueueType::Transfer);
		m_CommandAllocatorPool = dx12Device->GetCommandAllocatorPool(CommandQueueType::Transfer);
		GGLAB_ASSERT_MSG(m_CommandQueue != nullptr && m_CommandAllocatorPool != nullptr,
			"Transfer command queue or command allocator pool is null.");

		DX12CommandList::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_DX12Device;
		createInfo.m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		m_CommandList = std::make_unique<DX12CommandList>(createInfo);
	}

	RHITransferContext::~RHITransferContext() = default;

	void RHITransferContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "TextureBarrier must be called between Begin() and End().");
		if (!m_ExecutingInfo || barriers.empty())
		{
			return;
		}

		std::vector<CD3DX12_RESOURCE_BARRIER> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHITextureBarrier& barrier : barriers)
		{
			DX12Texture* texture = m_DX12Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::TextureBarrier received a non-live texture handle.");
				continue;
			}

			nativeBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
				texture->Get(),
				ToD3D12ResourceStates(barrier.m_Before),
				ToD3D12ResourceStates(barrier.m_After)));
			RecordTextureUse(barrier.m_Texture);
		}

		if (!nativeBarriers.empty())
		{
			m_CommandList->Get()->ResourceBarrier(
				static_cast<UINT>(nativeBarriers.size()),
				nativeBarriers.data());
		}
	}

	void RHITransferContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "BufferBarrier must be called between Begin() and End().");
		if (!m_ExecutingInfo || barriers.empty())
		{
			return;
		}

		std::vector<CD3DX12_RESOURCE_BARRIER> nativeBarriers;
		nativeBarriers.reserve(barriers.size());
		for (const RHIBufferBarrier& barrier : barriers)
		{
			DX12Buffer* buffer = m_DX12Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::BufferBarrier received a non-live buffer handle.");
				continue;
			}

			nativeBarriers.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
				buffer->Get(),
				ToD3D12ResourceStates(barrier.m_Before),
				ToD3D12ResourceStates(barrier.m_After)));
			RecordBufferUse(barrier.m_Buffer);
		}

		if (!nativeBarriers.empty())
		{
			m_CommandList->Get()->ResourceBarrier(
				static_cast<UINT>(nativeBarriers.size()),
				nativeBarriers.data());
		}
	}

	void RHITransferContext::Begin() noexcept
	{
		// Do this in TransferManager, not here.
		//ReclaimCompleted();

		GGLAB_ASSERT_MSG(!m_ExecutingInfo,
			"RHITransferContext::Begin() called while a transfer operation is already in progress.");
		GGLAB_ASSERT_MSG(m_CurrentCommandAllocator == nullptr,
			"RHITransferContext::Begin() found a non-null current command allocator.");

		m_ExecutingInfo = std::make_unique<InFlightInfo>();
		m_CurrentCommandAllocator = m_CommandAllocatorPool->RequestCommandAllocator();
		m_CommandList->Begin(m_CurrentCommandAllocator);
	}

	DX12FencePoint RHITransferContext::End(bool wait) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo,
			"RHITransferContext::End() called without a matching Begin().");

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

		for (const RHIBufferHandle buffer : m_ExecutingInfo->m_UsedBuffers)
		{
			m_DX12Device->RecordBufferUse(buffer, fencePoint);
		}
		for (const RHITextureHandle texture : m_ExecutingInfo->m_UsedTextures)
		{
			m_DX12Device->RecordTextureUse(texture, fencePoint);
		}

		m_ExecutingInfo->m_FencePoint = fencePoint;
		m_InFlightInfos.push_back(std::move(m_ExecutingInfo));

		return fencePoint;
	}

	RHIFencePoint RHITransferContext::Submit(bool wait) noexcept
	{
		return End(wait).ToRHI();
	}

	DX12FencePoint RHITransferContext::ReclaimCompleted() noexcept
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

	void RHITransferContext::CopyBuffer(DX12Buffer* dstBuffer, uint64_t dstOffset,
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

	void RHITransferContext::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
		RHIBufferHandle src, uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "CopyBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_DX12Device->ResolveBuffer(dst);
		DX12Buffer* srcBuffer = m_DX12Device->ResolveBuffer(src);
		if (!dstBuffer || !srcBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::CopyBuffer received a non-live RHI buffer handle.");
			return;
		}
		if (numBytes == 0 ||
			dstOffset > dstBuffer->SizeInBytes() ||
			numBytes > dstBuffer->SizeInBytes() - dstOffset ||
			srcOffset > srcBuffer->SizeInBytes() ||
			numBytes > srcBuffer->SizeInBytes() - srcOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::CopyBuffer received an invalid RHI buffer copy range.");
			return;
		}

		CopyBuffer(dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
		RecordBufferUse(dst);
		RecordBufferUse(src);
	}

	bool RHITransferContext::UploadBuffer(const void* data, uint64_t sizeInBytes,
		RHIBufferHandle dst, uint64_t dstOffset) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_DX12Device->ResolveBuffer(dst);
		if (!dstBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::UploadBuffer received a non-live RHI buffer handle.");
			return false;
		}
		if (!data ||
			sizeInBytes == 0 ||
			dstOffset > dstBuffer->SizeInBytes() ||
			sizeInBytes > dstBuffer->SizeInBytes() - dstOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::UploadBuffer received invalid input or range.");
			return false;
		}

		auto uploadBuffer = std::make_unique<DX12Buffer>();
		uploadBuffer->Create(DX12Buffer::UploadBufferCreateInfo(m_DX12Device->GetMemAllocator(), sizeInBytes));
		uploadBuffer->SetDebugName(L"RHITransferContext.UploadIntermediateBuffer");
		if (!uploadBuffer->IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR("RHITransferContext::UploadBuffer failed to create an intermediate upload buffer.");
			return false;
		}

		void* mappedData = uploadBuffer->Map();
		if (!mappedData)
		{
			GGLAB_LOG_GRAPHICS_ERROR("RHITransferContext::UploadBuffer failed to map the intermediate upload buffer.");
			return false;
		}

		std::memcpy(mappedData, data, static_cast<size_t>(sizeInBytes));
		uploadBuffer->Unmap();

		CopyBuffer(dstBuffer, dstOffset, uploadBuffer.get(), 0, sizeInBytes);
		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		RecordBufferUse(dst);
		return true;
	}

	void RHITransferContext::UploadTexture(
		const RHITextureUploadData& uploadData,
		RHITextureHandle dst) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadTexture must be called between Begin() and End().");

		DX12Texture* dstTexture = m_DX12Device->ResolveTexture(dst);
		if (!dstTexture)
		{
			GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::UploadTexture received a non-live RHI texture handle.");
			return;
		}

		std::vector<D3D12_SUBRESOURCE_DATA> nativeSubresources;
		nativeSubresources.reserve(uploadData.m_Subresources.size());
		for (const RHITextureSubresourceData& subresource : uploadData.m_Subresources)
		{
			if (!subresource.m_Data || subresource.m_RowPitch == 0 || subresource.m_SlicePitch == 0)
			{
				GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::UploadTexture skipped an invalid texture subresource.");
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
			GGLAB_LOG_GRAPHICS_ERROR("RHITransferContext::UploadTexture received no valid texture subresources.");
			return;
		}

		UploadResource(nativeSubresources, dstTexture);
		RecordTextureUse(dst);
	}

	void RHITransferContext::UploadResource(const void* data, uint64_t sizeInBytes,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");

		auto uploadBuffer = std::make_unique<DX12Buffer>();
		uploadBuffer->Create(DX12Buffer::UploadBufferCreateInfo(m_DX12Device->GetMemAllocator(), sizeInBytes));
		uploadBuffer->SetDebugName(L"RHITransferContext.UploadIntermediateBuffer");

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

	void RHITransferContext::UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");

		auto subResourceCount = static_cast<UINT>(subResources.size());
		auto uploadSize = GetRequiredIntermediateSize(dstResource->Get(), 0, subResourceCount);

		auto uploadBuffer = std::make_unique<DX12Buffer>();
		uploadBuffer->Create(DX12Buffer::UploadBufferCreateInfo(m_DX12Device->GetMemAllocator(), static_cast<uint64_t>(uploadSize)));
		uploadBuffer->SetDebugName(L"RHITransferContext.UploadIntermediateBuffer");

		UpdateSubresources(m_CommandList->Get(),
			dstResource->Get(),
			uploadBuffer->Get(),
			0, 0, subResourceCount, subResources.data());

		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
	}

	void RHITransferContext::UploadResource(
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
				GGLAB_LOG_GRAPHICS_WARN("RHITransferContext::UploadResource skipped an invalid texture subresource.");
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
			GGLAB_LOG_GRAPHICS_ERROR("RHITransferContext::UploadResource received no valid texture subresources.");
			return;
		}

		UploadResource(nativeSubresources, dstResource);
	}

	void RHITransferContext::RecordBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (!m_ExecutingInfo || !buffer.IsValid())
		{
			return;
		}

		m_ExecutingInfo->m_UsedBuffers.push_back(buffer);
	}

	void RHITransferContext::RecordTextureUse(RHITextureHandle texture) noexcept
	{
		if (!m_ExecutingInfo || !texture.IsValid())
		{
			return;
		}

		m_ExecutingInfo->m_UsedTextures.push_back(texture);
	}
}
