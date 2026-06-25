#pragma once
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class DX12Device;
	class DX12Buffer;
	class DX12Resource;
	class DX12CommandQueue;
	class DX12CommandAllocator;
	class DX12CommandAllocatorPool;
	class DX12CommandList;

	class RHITransferContext final : public RHICommandContext
	{
	public:
		explicit RHITransferContext(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(RHITransferContext);
		~RHITransferContext() override;

		RHICommandContextHandle GetHandle() const noexcept override { return m_Handle; }
		RHIQueueType GetQueueType() const noexcept override { return RHIQueueType::Copy; }
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;

		void Begin() noexcept;
		DX12FencePoint End(bool wait = false) noexcept;
		RHIFencePoint Submit(bool wait = false) noexcept;
		DX12FencePoint ReclaimCompleted() noexcept;

		void CopyBuffer(DX12Buffer* dst, uint64_t dstOffset,
			DX12Buffer* src, uint64_t srcOffset,
			uint64_t numBytes) noexcept;
		void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset,
			RHIBufferHandle src, uint64_t srcOffset,
			uint64_t numBytes) noexcept;
		void UploadBuffer(const void* data, uint64_t sizeInBytes,
			RHIBufferHandle dst, uint64_t dstOffset = 0) noexcept;
		void UploadTexture(const RHITextureUploadData& uploadData,
			RHITextureHandle dst) noexcept;

		// Transitional DX12 helpers. Keep these while legacy resource owners still
		// pass native resources into the transfer path.
		void UploadResource(const void* data, uint64_t sizeInBytes,
			const DX12Resource* dstResource) noexcept;
		void UploadResource(const RHITextureUploadData& uploadData,
			const DX12Resource* dstResource) noexcept;
		void UploadResource(const std::vector<D3D12_SUBRESOURCE_DATA>& subResources,
			const DX12Resource* dstResource) noexcept;

	private:
		struct InFlightInfo
		{
			DX12FencePoint m_FencePoint;
			std::vector<std::unique_ptr<DX12Buffer>> m_IntermediateBuffers;
			std::vector<RHIBufferHandle> m_UsedBuffers;
			std::vector<RHITextureHandle> m_UsedTextures;
		};

		void RecordBufferUse(RHIBufferHandle buffer) noexcept;
		void RecordTextureUse(RHITextureHandle texture) noexcept;

		RHICommandContextHandle m_Handle{};
		DX12Device* m_DX12Device = nullptr;
		DX12CommandQueue* m_CommandQueue = nullptr;
		DX12CommandAllocatorPool* m_CommandAllocatorPool = nullptr;
		DX12CommandAllocator* m_CurrentCommandAllocator = nullptr;
		std::unique_ptr<DX12CommandList> m_CommandList = nullptr;

		std::unique_ptr<InFlightInfo> m_ExecutingInfo = nullptr;
		std::vector<std::unique_ptr<InFlightInfo>> m_InFlightInfos;
	};
}
