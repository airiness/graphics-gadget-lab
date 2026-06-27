#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

namespace gglab
{
	class RHITransferContext : public RHICommandContext
	{
	public:
		~RHITransferContext() override = default;

		virtual void Begin() noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint Submit(bool wait = false) noexcept = 0;
		virtual void ReclaimCompleted() noexcept = 0;

		virtual void CopyBuffer(
			RHIBufferHandle dst,
			uint64_t dstOffset,
			RHIBufferHandle src,
			uint64_t srcOffset,
			uint64_t numBytes) noexcept = 0;
		[[nodiscard]] virtual bool UploadBuffer(
			const void* data,
			uint64_t sizeInBytes,
			RHIBufferHandle dst,
			uint64_t dstOffset = 0) noexcept = 0;
		[[nodiscard]] virtual bool UploadTexture(
			const RHITextureUploadData& uploadData,
			RHITextureHandle dst) noexcept = 0;
	};
}
