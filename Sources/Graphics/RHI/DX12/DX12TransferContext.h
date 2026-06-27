#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/RHITransferContext.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12Resource;
	class DX12CommandQueue;
	class DX12CommandAllocator;
	class DX12CommandAllocatorPool;
	class DX12CommandList;

	class DX12TransferContext final : public RHITransferContext
	{
	public:
		explicit DX12TransferContext(DX12Device* device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12TransferContext);
		~DX12TransferContext() override;

		RHICommandContextHandle GetHandle() const noexcept override { return m_Handle; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Transfer; }
		void TrackTextureUse(RHITextureHandle texture) noexcept override { RecordTextureUse(texture); }
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override { RecordBufferUse(buffer); }
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;

		void Begin() noexcept override;
		RHIFencePoint Submit(bool wait = false) noexcept override;
		void ReclaimCompleted() noexcept override;
		void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
			RHIBufferHandle src, uint64_t srcOffset,
			uint64_t numBytes) noexcept override;
		bool UploadBuffer(const void* data, uint64_t sizeInBytes,
			RHIBufferHandle dst, uint64_t dstOffset = 0) noexcept override;
		bool UploadTexture(const RHITextureUploadData& uploadData,
			RHITextureHandle dst) noexcept override;

	private:
		struct InFlightInfo
		{
			DX12FencePoint m_FencePoint;
			std::vector<RHIBufferOwner> m_IntermediateBuffers;
			std::vector<RHIBufferHandle> m_UsedBuffers;
			std::vector<RHITextureHandle> m_UsedTextures;
		};

		DX12FencePoint End(bool wait) noexcept;
		RHIBufferOwner CreateUploadBuffer(uint64_t sizeInBytes) noexcept;
		void CopyBuffer(DX12Buffer* dst, uint64_t dstOffset,
			DX12Buffer* src, uint64_t srcOffset,
			uint64_t numBytes) noexcept;
		bool UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
			const DX12Resource* dstResource) noexcept;
		void RecordBufferUse(RHIBufferHandle buffer) noexcept;
		void RecordTextureUse(RHITextureHandle texture) noexcept;

		RHICommandContextHandle m_Handle{};
		DX12Device* m_Device = nullptr;
		DX12CommandQueue* m_CommandQueue = nullptr;
		DX12CommandAllocatorPool* m_CommandAllocatorPool = nullptr;
		DX12CommandAllocator* m_CurrentCommandAllocator = nullptr;
		std::unique_ptr<DX12CommandList> m_CommandList;
		std::unique_ptr<InFlightInfo> m_ExecutingInfo;
		std::vector<std::unique_ptr<InFlightInfo>> m_InFlightInfos;
	};
}
