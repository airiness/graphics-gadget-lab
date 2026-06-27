#include "Core/Precompiled.h"
#include "Graphics/RHI/DX12/DX12TransferContext.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/DX12/Utility/DX12QueueUtils.h"

#include <cstring>

namespace gglab
{
	DX12TransferContext::DX12TransferContext(DX12Device* dx12Device) noexcept :
		m_Handle(AllocateDX12CommandContextHandle()),
		m_Device(dx12Device)
	{
		GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device pointer can not be null.");

		m_CommandQueue = dx12Device->GetCommandQueue(CommandQueueType::Transfer);
		m_CommandAllocatorPool = dx12Device->GetCommandAllocatorPool(CommandQueueType::Transfer);
		GGLAB_ASSERT_MSG(m_CommandQueue != nullptr && m_CommandAllocatorPool != nullptr,
			"Transfer command queue or command allocator pool is null.");

		DX12CommandList::CreateInfo createInfo{};
		createInfo.m_DX12Device = m_Device;
		createInfo.m_Type = ToD3D12CommandListType(GetQueueType());
		m_CommandList = std::make_unique<DX12CommandList>(createInfo);
	}

	DX12TransferContext::~DX12TransferContext()
	{
		GGLAB_ASSERT_MSG(!m_ExecutingInfo,
			"DX12TransferContext destroyed while command recording is active.");
		for (const auto& info : m_InFlightInfos)
		{
			info->m_FencePoint.Wait();
		}
		m_InFlightInfos.clear();
	}

	void DX12TransferContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
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
			DX12Texture* texture = m_Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::TextureBarrier received a non-live texture handle.");
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

	void DX12TransferContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
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
			DX12Buffer* buffer = m_Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::BufferBarrier received a non-live buffer handle.");
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

	void DX12TransferContext::Begin() noexcept
	{
		// Do this in TransferManager, not here.
		//ReclaimCompleted();

		GGLAB_ASSERT_MSG(!m_ExecutingInfo,
			"DX12TransferContext::Begin() called while a transfer operation is already in progress.");
		GGLAB_ASSERT_MSG(m_CurrentCommandAllocator == nullptr,
			"DX12TransferContext::Begin() found a non-null current command allocator.");

		m_ExecutingInfo = std::make_unique<InFlightInfo>();
		m_CurrentCommandAllocator = m_CommandAllocatorPool->RequestCommandAllocator();
		m_CommandList->Begin(m_CurrentCommandAllocator);
	}

	DX12FencePoint DX12TransferContext::End(bool wait) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo,
			"DX12TransferContext::End() called without a matching Begin().");

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
			m_Device->RecordBufferUse(buffer, fencePoint);
		}
		for (const RHITextureHandle texture : m_ExecutingInfo->m_UsedTextures)
		{
			m_Device->RecordTextureUse(texture, fencePoint);
		}

		m_ExecutingInfo->m_FencePoint = fencePoint;
		m_InFlightInfos.push_back(std::move(m_ExecutingInfo));

		return fencePoint;
	}

	RHIFencePoint DX12TransferContext::Submit(bool wait) noexcept
	{
		return End(wait).ToRHI();
	}

	void DX12TransferContext::ReclaimCompleted() noexcept
	{
		auto iter = std::remove_if(m_InFlightInfos.begin(), m_InFlightInfos.end(),
			[](const std::unique_ptr<InFlightInfo>& info)
			{
				return info->m_FencePoint.IsCompleted();
			});

		m_InFlightInfos.erase(iter, m_InFlightInfos.end());
	}

	void DX12TransferContext::CopyBuffer(DX12Buffer* dstBuffer, uint64_t dstOffset,
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

	void DX12TransferContext::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
		RHIBufferHandle src, uint64_t srcOffset,
		uint64_t numBytes) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "CopyBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_Device->ResolveBuffer(dst);
		DX12Buffer* srcBuffer = m_Device->ResolveBuffer(src);
		if (!dstBuffer || !srcBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::CopyBuffer received a non-live RHI buffer handle.");
			return;
		}
		if (numBytes == 0 ||
			dstOffset > dstBuffer->SizeInBytes() ||
			numBytes > dstBuffer->SizeInBytes() - dstOffset ||
			srcOffset > srcBuffer->SizeInBytes() ||
			numBytes > srcBuffer->SizeInBytes() - srcOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::CopyBuffer received an invalid RHI buffer copy range.");
			return;
		}

		CopyBuffer(dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
		RecordBufferUse(dst);
			RecordBufferUse(src);
	}

	RHIBufferOwner DX12TransferContext::CreateUploadBuffer(uint64_t sizeInBytes) noexcept
	{
		RHIBufferDesc desc{};
		desc.m_SizeInBytes = sizeInBytes;
		desc.m_Usage = RHIBufferUsage::CopySource;
		desc.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
		desc.m_DebugName = "DX12TransferContext.UploadIntermediateBuffer";
		return RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc));
	}

	bool DX12TransferContext::UploadBuffer(const void* data, uint64_t sizeInBytes,
		RHIBufferHandle dst, uint64_t dstOffset) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_Device->ResolveBuffer(dst);
		if (!dstBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::UploadBuffer received a non-live RHI buffer handle.");
			return false;
		}
		if (!data ||
			sizeInBytes == 0 ||
			dstOffset > dstBuffer->SizeInBytes() ||
			sizeInBytes > dstBuffer->SizeInBytes() - dstOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::UploadBuffer received invalid input or range.");
			return false;
		}

		RHIBufferOwner uploadBuffer = CreateUploadBuffer(sizeInBytes);
		if (!uploadBuffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12TransferContext::UploadBuffer failed to create an intermediate upload buffer.");
			return false;
		}

		void* mappedData = m_Device->MapBuffer(uploadBuffer.Get());
		if (!mappedData)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12TransferContext::UploadBuffer failed to map the intermediate upload buffer.");
			return false;
		}

		std::memcpy(mappedData, data, static_cast<size_t>(sizeInBytes));
		m_Device->UnmapBuffer(uploadBuffer.Get());

		DX12Buffer* nativeUploadBuffer = m_Device->ResolveBuffer(uploadBuffer.Get());
		GGLAB_ASSERT_NOT_NULL(nativeUploadBuffer);
		CopyBuffer(dstBuffer, dstOffset, nativeUploadBuffer, 0, sizeInBytes);
		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		RecordBufferUse(dst);
		return true;
	}

	bool DX12TransferContext::UploadTexture(
		const RHITextureUploadData& uploadData,
		RHITextureHandle dst) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadTexture must be called between Begin() and End().");

		DX12Texture* dstTexture = m_Device->ResolveTexture(dst);
		if (!dstTexture)
		{
			GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::UploadTexture received a non-live RHI texture handle.");
			return false;
		}

		std::vector<D3D12_SUBRESOURCE_DATA> nativeSubresources;
		nativeSubresources.reserve(uploadData.m_Subresources.size());
		for (const RHITextureSubresourceData& subresource : uploadData.m_Subresources)
		{
			if (!subresource.m_Data || subresource.m_RowPitch == 0 || subresource.m_SlicePitch == 0)
			{
				GGLAB_LOG_GRAPHICS_WARN("DX12TransferContext::UploadTexture skipped an invalid texture subresource.");
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
			GGLAB_LOG_GRAPHICS_ERROR("DX12TransferContext::UploadTexture received no valid texture subresources.");
			return false;
		}

		if (!UploadResource(nativeSubresources, dstTexture))
		{
			return false;
		}
		RecordTextureUse(dst);
		return true;
	}

	bool DX12TransferContext::UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
		const DX12Resource* dstResource) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadResource must be called between Begin() and End().");
		if (!m_ExecutingInfo || !dstResource || !dstResource->IsValid() || subResources.empty())
		{
			return false;
		}

		auto subResourceCount = static_cast<UINT>(subResources.size());
		auto uploadSize = GetRequiredIntermediateSize(dstResource->Get(), 0, subResourceCount);

		RHIBufferOwner uploadBuffer = CreateUploadBuffer(static_cast<uint64_t>(uploadSize));
		if (!uploadBuffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DX12TransferContext failed to create a texture upload buffer.");
			return false;
		}
		DX12Buffer* nativeUploadBuffer = m_Device->ResolveBuffer(uploadBuffer.Get());
		GGLAB_ASSERT_NOT_NULL(nativeUploadBuffer);

		const uint64_t uploadedBytes = UpdateSubresources(m_CommandList->Get(),
			dstResource->Get(),
			nativeUploadBuffer->Get(),
			0, 0, subResourceCount, subResources.data());

		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		return uploadedBytes > 0;
	}

	void DX12TransferContext::RecordBufferUse(RHIBufferHandle buffer) noexcept
	{
		if (!m_ExecutingInfo || !buffer.IsValid())
		{
			return;
		}

		m_ExecutingInfo->m_UsedBuffers.push_back(buffer);
	}

	void DX12TransferContext::RecordTextureUse(RHITextureHandle texture) noexcept
	{
		if (!m_ExecutingInfo || !texture.IsValid())
		{
			return;
		}

		m_ExecutingInfo->m_UsedTextures.push_back(texture);
	}
}
