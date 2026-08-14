#include "Graphics/RHI/DX12/DX12TransferContext.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/DX12/DX12CommandContext.h"
#include "Graphics/RHI/DX12/DX12Device.h"
#include "Graphics/RHI/DX12/DX12Buffer.h"
#include "Graphics/RHI/DX12/DX12CommandList.h"
#include "Graphics/RHI/DX12/DX12CommandQueue.h"
#include "Graphics/RHI/DX12/DX12CommandAllocator.h"
#include "Graphics/RHI/DX12/DX12QueueSystem.h"
#include "Graphics/RHI/DX12/DX12Texture.h"
#include "Graphics/RHI/DX12/Utility/DX12BarrierUtils.h"
#include "Graphics/RHI/RHITextureValidation.h"
#include "Graphics/Utility/DXGIFormatUtils.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] RHITextureDesc MakeRHITextureDesc(
			const D3D12_RESOURCE_DESC& nativeDesc) noexcept
		{
			RHITextureDesc desc{};
			desc.m_Format = ToRHIFormat(nativeDesc.Format);
			desc.m_Usage = RHITextureUsage::CopyDest;
			desc.m_Extent.m_Width = static_cast<uint32_t>(nativeDesc.Width);
			desc.m_Extent.m_Height = nativeDesc.Height;
			desc.m_MipLevels = nativeDesc.MipLevels;
			desc.m_SampleCount = static_cast<uint16_t>(nativeDesc.SampleDesc.Count);

			switch (nativeDesc.Dimension)
			{
			case D3D12_RESOURCE_DIMENSION_TEXTURE1D:
				desc.m_Dimension = RHITextureDimension::Texture1D;
				desc.m_Extent.m_Depth = 1;
				desc.m_ArraySize = nativeDesc.DepthOrArraySize;
				break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE2D:
				desc.m_Dimension = RHITextureDimension::Texture2D;
				desc.m_Extent.m_Depth = 1;
				desc.m_ArraySize = nativeDesc.DepthOrArraySize;
				break;
			case D3D12_RESOURCE_DIMENSION_TEXTURE3D:
				desc.m_Dimension = RHITextureDimension::Texture3D;
				desc.m_Extent.m_Depth = nativeDesc.DepthOrArraySize;
				desc.m_ArraySize = 1;
				break;
			default:
				desc.m_Dimension = static_cast<RHITextureDimension>(-1);
				break;
			}
			return desc;
		}
	}

	DX12TransferContext::DX12TransferContext(
		DX12Device* dx12Device, DX12QueueSystem* queueSystem) noexcept :
		m_Handle(AllocateRHICommandContextHandle()), m_Device(dx12Device)
	{
		GGLAB_ASSERT_MSG(dx12Device != nullptr, "DX12Device pointer can not be null.");
		GGLAB_ASSERT_MSG(queueSystem != nullptr, "DX12QueueSystem pointer can not be null.");

		m_CommandQueue = &queueSystem->GetQueue(DX12QueueType::Transfer);
		m_CommandAllocatorPool = &queueSystem->GetAllocatorPool(DX12QueueType::Transfer);
		GGLAB_ASSERT_MSG(m_CommandQueue != nullptr && m_CommandAllocatorPool != nullptr,
			"Transfer command queue or command allocator pool is null.");

		m_CommandList = queueSystem->CreateCommandList(DX12QueueType::Transfer);
	}

	DX12TransferContext::~DX12TransferContext()
	{
		GGLAB_ASSERT_MSG(
			!m_ExecutingInfo, "DX12TransferContext destroyed while command recording is active.");
		for (const auto& info : m_InFlightInfos)
		{
			info->m_FencePoint.Wait();
		}
		m_InFlightInfos.clear();
	}

	void DX12TransferContext::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "TextureBarrier must be called between Begin() and End().");
		if (!m_ExecutingInfo || barriers.empty())
		{
			return;
		}

		bool barrierRecorded = false;
		for (const RHITextureBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(
				barrier.m_Before, RHIResourceStateUsage::TextureBarrierBefore) ||
				!IsRHIResourceStateValid(
					barrier.m_After, RHIResourceStateUsage::TextureBarrierAfter))
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12TransferContext::TextureBarrier rejected an invalid resource state.");
				continue;
			}
			DX12Texture* texture = m_Device->ResolveTexture(barrier.m_Texture);
			if (!texture)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12TransferContext::TextureBarrier received a non-live texture handle.");
				continue;
			}

			m_CommandList->AddTextureBarrier(
				BuildD3D12TextureBarrier(barrier, texture->Get(), texture->GetDesc()));
			RecordTextureUse(barrier.m_Texture);
			barrierRecorded = true;
		}

		if (barrierRecorded)
		{
			FlushBarriers();
		}
	}

	void DX12TransferContext::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "BufferBarrier must be called between Begin() and End().");
		if (!m_ExecutingInfo || barriers.empty())
		{
			return;
		}

		bool barrierRecorded = false;
		for (const RHIBufferBarrier& barrier : barriers)
		{
			if (!IsRHIResourceStateValid(barrier.m_Before, RHIResourceStateUsage::Buffer) ||
				!IsRHIResourceStateValid(barrier.m_After, RHIResourceStateUsage::Buffer))
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12TransferContext::BufferBarrier rejected an invalid resource state.");
				continue;
			}
			DX12Buffer* buffer = m_Device->ResolveBuffer(barrier.m_Buffer);
			if (!buffer)
			{
				GGLAB_LOG_GRAPHICS_WARN(
					"DX12TransferContext::BufferBarrier received a non-live buffer handle.");
				continue;
			}

			m_CommandList->AddBufferBarrier(BuildD3D12BufferBarrier(barrier, buffer->Get()));
			RecordBufferUse(barrier.m_Buffer);
			barrierRecorded = true;
		}

		if (barrierRecorded)
		{
			FlushBarriers();
		}
	}

	void DX12TransferContext::FlushBarriers() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "FlushBarriers must be called between Begin() and End().");
		if (m_ExecutingInfo)
		{
			m_CommandList->FlushBarriers();
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
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "DX12TransferContext::End() called without a matching Begin().");

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

	void DX12TransferContext::Abort() noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo,
			"DX12TransferContext::Abort() called without an active transfer operation.");
		if (!m_ExecutingInfo)
		{
			return;
		}

		// Close but do not execute the discarded command list. Signaling the
		// queue gives the allocator pool a normal retirement point without
		// exposing DX12 allocator lifetime rules through the RHI interface.
		m_CommandList->End();
		m_ExecutingInfo.reset();
		const DX12FencePoint fencePoint = m_CommandQueue->Signal();
		m_CommandAllocatorPool->RecycleCommandAllocator(m_CurrentCommandAllocator, fencePoint);
		m_CurrentCommandAllocator = nullptr;
	}

	void DX12TransferContext::ReclaimCompleted() noexcept
	{
		auto iter = std::remove_if(m_InFlightInfos.begin(), m_InFlightInfos.end(),
			[](const std::unique_ptr<InFlightInfo>& info)
			{ return info->m_FencePoint.IsCompleted(); });

		m_InFlightInfos.erase(iter, m_InFlightInfos.end());
	}

	void DX12TransferContext::CopyBuffer(DX12Buffer* dstBuffer, uint64_t dstOffset,
		DX12Buffer* srcBuffer, uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "CopyBuffer must be called between Begin() and End().");

		auto* commandList = m_CommandList->Get();
		GGLAB_ASSERT(commandList && dstBuffer && srcBuffer && numBytes);
		commandList->CopyBufferRegion(
			dstBuffer->Get(), dstOffset, srcBuffer->Get(), srcOffset, numBytes);
	}

	void DX12TransferContext::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
		RHIBufferHandle src, uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "CopyBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_Device->ResolveBuffer(dst);
		DX12Buffer* srcBuffer = m_Device->ResolveBuffer(src);
		if (!dstBuffer || !srcBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::CopyBuffer received a non-live RHI buffer handle.");
			return;
		}
		if (numBytes == 0 || dstOffset > dstBuffer->SizeInBytes() ||
			numBytes > dstBuffer->SizeInBytes() - dstOffset ||
			srcOffset > srcBuffer->SizeInBytes() || numBytes > srcBuffer->SizeInBytes() - srcOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::CopyBuffer received an invalid RHI buffer copy range.");
			return;
		}

		CopyBuffer(dstBuffer, dstOffset, srcBuffer, srcOffset, numBytes);
		RecordBufferUse(dst);
		RecordBufferUse(src);
	}

	RHIBufferOwner DX12TransferContext::CreateUploadBuffer(
		uint64_t sizeInBytes, std::string_view owner) noexcept
	{
		RHIBufferDesc desc{};
		desc.m_SizeInBytes = sizeInBytes;
		desc.m_Usage = RHIBufferUsage::CopySource;
		desc.m_MemoryUsage = RHIMemoryUsage::CpuToGpu;
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Transfer,
			.m_Category = "UploadBuffer",
			.m_Label = owner,
			.m_StableId = m_NextDebugOperationSerial++,
		};
		return RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc, debugIdentity));
	}

	RHIBufferOwner DX12TransferContext::CreateReadbackBuffer(
		uint64_t sizeInBytes, std::string_view owner) noexcept
	{
		RHIBufferDesc desc{};
		desc.m_SizeInBytes = sizeInBytes;
		desc.m_Usage = RHIBufferUsage::CopyDest;
		desc.m_MemoryUsage = RHIMemoryUsage::GpuToCpu;
		const RHIResourceDebugIdentityDesc debugIdentity{
			.m_Domain = RHIResourceDebugDomain::Transfer,
			.m_Category = "TextureReadbackBuffer",
			.m_Label = owner,
			.m_StableId = m_NextDebugOperationSerial++,
		};
		return RHIBufferOwner(m_Device, m_Device->CreateBuffer(desc, debugIdentity));
	}

	bool DX12TransferContext::UploadBuffer(
		const void* data, uint64_t sizeInBytes, RHIBufferHandle dst, uint64_t dstOffset) noexcept
	{
		GGLAB_ASSERT_MSG(m_ExecutingInfo, "UploadBuffer must be called between Begin() and End().");

		DX12Buffer* dstBuffer = m_Device->ResolveBuffer(dst);
		if (!dstBuffer)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::UploadBuffer received a non-live RHI buffer handle.");
			return false;
		}
		if (!data || sizeInBytes == 0 || dstOffset > dstBuffer->SizeInBytes() ||
			sizeInBytes > dstBuffer->SizeInBytes() - dstOffset)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::UploadBuffer received invalid input or range.");
			return false;
		}

		const std::string uploadOwner =
			std::format("BufferUpload->RHI={}:{}", dst.Index(), dst.Generation());
		RHIBufferOwner uploadBuffer = CreateUploadBuffer(sizeInBytes, uploadOwner);
		if (!uploadBuffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12TransferContext::UploadBuffer failed to create an intermediate upload buffer.");
			return false;
		}

		void* mappedData = m_Device->MapBuffer(uploadBuffer.Get(), {});
		if (!mappedData)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12TransferContext::UploadBuffer failed to map the intermediate upload buffer.");
			return false;
		}

		std::memcpy(mappedData, data, static_cast<size_t>(sizeInBytes));
		m_Device->UnmapBuffer(uploadBuffer.Get(), { 0, sizeInBytes });

		DX12Buffer* nativeUploadBuffer = m_Device->ResolveBuffer(uploadBuffer.Get());
		GGLAB_ASSERT_NOT_NULL(nativeUploadBuffer);
		CopyBuffer(dstBuffer, dstOffset, nativeUploadBuffer, 0, sizeInBytes);
		m_ExecutingInfo->m_IntermediateBuffers.push_back(std::move(uploadBuffer));
		RecordBufferUse(dst);
		return true;
	}

	bool DX12TransferContext::UploadTexture(
		const RHITextureUploadData& uploadData, RHITextureHandle dst) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "UploadTexture must be called between Begin() and End().");

		DX12Texture* dstTexture = m_Device->ResolveTexture(dst);
		if (!dstTexture)
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::UploadTexture received a non-live RHI texture handle.");
			return false;
		}

		const RHITextureDesc textureDesc = MakeRHITextureDesc(dstTexture->Get()->GetDesc());
		const RHITextureValidationResult validation =
			ValidateRHITextureUploadData(textureDesc, uploadData);
		if (!validation.IsValid())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12TransferContext::UploadTexture rejected the complete upload: {}.",
				RHITextureValidationErrorText(validation.m_Error));
			return false;
		}

		std::vector<D3D12_SUBRESOURCE_DATA> nativeSubresources;
		nativeSubresources.reserve(uploadData.m_Subresources.size());
		for (const RHITextureSubresourceData& subresource : uploadData.m_Subresources)
		{
			nativeSubresources.push_back({
				.pData = subresource.m_Data,
				.RowPitch = static_cast<LONG_PTR>(subresource.m_RowPitch),
				.SlicePitch = static_cast<LONG_PTR>(subresource.m_SlicePitch),
				});
		}

		const std::string uploadOwner =
			std::format("TextureUpload->RHI={}:{}", dst.Index(), dst.Generation());
		if (!UploadResource(nativeSubresources, dstTexture, uploadOwner))
		{
			return false;
		}
		RecordTextureUse(dst);
		return true;
	}

	RHITextureReadbackRequest DX12TransferContext::ReadbackTexture(
		RHITextureHandle src, const RHITextureDesc& desc) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "ReadbackTexture must be called between Begin() and End().");

		DX12Texture* srcTexture = m_Device->ResolveTexture(src);
		if (!srcTexture || !srcTexture->IsValid())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DX12TransferContext::ReadbackTexture received a non-live texture handle.");
			return {};
		}

		const uint32_t subresourceCount = static_cast<uint32_t>(desc.m_MipLevels) *
			static_cast<uint32_t>(desc.m_ArraySize) *
			GetRHITexturePlaneCount(desc);
		if (subresourceCount == 0)
		{
			return {};
		}

		const D3D12_RESOURCE_DESC nativeDesc = srcTexture->Get()->GetDesc();
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(subresourceCount);
		std::vector<UINT> rowCounts(subresourceCount);
		std::vector<UINT64> rowSizes(subresourceCount);
		UINT64 totalBytes = 0;
		m_Device->Get()->GetCopyableFootprints(&nativeDesc, 0, subresourceCount, 0,
			footprints.data(), rowCounts.data(), rowSizes.data(), &totalBytes);

		RHITextureReadbackRequest request{};
		const std::string readbackOwner =
			std::format("TextureReadback<-RHI={}:{}", src.Index(), src.Generation());
		request.m_Buffer = CreateReadbackBuffer(totalBytes, readbackOwner);
		request.m_BufferSizeInBytes = totalBytes;
		request.m_TextureDesc = desc;
		request.m_TextureDesc.m_DebugName = nullptr;
		if (!request.m_Buffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12TransferContext::ReadbackTexture failed to create a readback buffer.");
			return {};
		}

		DX12Buffer* readbackBuffer = m_Device->ResolveBuffer(request.m_Buffer.Get());
		GGLAB_ASSERT_NOT_NULL(readbackBuffer);
		request.m_Subresources.reserve(subresourceCount);
		for (uint32_t subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
		{
			D3D12_TEXTURE_COPY_LOCATION dstLocation{};
			dstLocation.pResource = readbackBuffer->Get();
			dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			dstLocation.PlacedFootprint = footprints[subresourceIndex];

			D3D12_TEXTURE_COPY_LOCATION srcLocation{};
			srcLocation.pResource = srcTexture->Get();
			srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			srcLocation.SubresourceIndex = subresourceIndex;
			m_CommandList->Get()->CopyTextureRegion(&dstLocation, 0, 0, 0, &srcLocation, nullptr);

			const uint32_t mipLevel = subresourceIndex % desc.m_MipLevels;
			const uint32_t arraySlice = (subresourceIndex / desc.m_MipLevels) % desc.m_ArraySize;
			const auto& footprint = footprints[subresourceIndex];
			request.m_Subresources.push_back({
				.m_BufferOffset = footprint.Offset,
				.m_RowPitch = footprint.Footprint.RowPitch,
				.m_RowSizeInBytes = rowSizes[subresourceIndex],
				.m_SlicePitch = static_cast<uint64_t>(footprint.Footprint.RowPitch) *
								rowCounts[subresourceIndex],
				.m_RowCount = rowCounts[subresourceIndex],
				.m_Width = footprint.Footprint.Width,
				.m_Height = footprint.Footprint.Height,
				.m_Depth = footprint.Footprint.Depth,
				.m_MipLevel = mipLevel,
				.m_ArraySlice = arraySlice,
				});
		}

		RecordTextureUse(src);
		RecordBufferUse(request.m_Buffer.Get());
		return request;
	}

	bool DX12TransferContext::UploadResource(
		const std::vector<D3D12_SUBRESOURCE_DATA>& subResources, const DX12Resource* dstResource,
		std::string_view owner) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_ExecutingInfo, "UploadResource must be called between Begin() and End().");
		if (!m_ExecutingInfo || !dstResource || !dstResource->IsValid() || subResources.empty())
		{
			return false;
		}

		auto subResourceCount = static_cast<UINT>(subResources.size());
		auto uploadSize = GetRequiredIntermediateSize(dstResource->Get(), 0, subResourceCount);

		RHIBufferOwner uploadBuffer = CreateUploadBuffer(static_cast<uint64_t>(uploadSize), owner);
		if (!uploadBuffer)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DX12TransferContext failed to create a texture upload buffer.");
			return false;
		}
		DX12Buffer* nativeUploadBuffer = m_Device->ResolveBuffer(uploadBuffer.Get());
		GGLAB_ASSERT_NOT_NULL(nativeUploadBuffer);

		const uint64_t uploadedBytes = UpdateSubresources(m_CommandList->Get(), dstResource->Get(),
			nativeUploadBuffer->Get(), 0, 0, subResourceCount, subResources.data());

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
