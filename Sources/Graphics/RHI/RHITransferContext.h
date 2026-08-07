#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHICommandContext.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

#include <vector>

namespace gglab
{
	struct RHITransferResourcePublication
	{
		RHIResourceType m_Type = RHIResourceType::Unknown;
		RHITextureHandle m_Texture{};
		RHIBufferHandle m_Buffer{};
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
		RHIResourceState m_PublishedState{};
	};

	struct RHITransferSubmission
	{
		RHIFencePoint m_Completion;
		std::vector<RHITransferResourcePublication> m_Publications;
	};

	class RHITransferContext : public RHICommandContext
	{
	public:
		~RHITransferContext() override = default;

		virtual void Begin() noexcept = 0;
		[[nodiscard]] virtual RHIFencePoint Submit(bool wait = false) noexcept = 0;
		virtual void Abort() noexcept = 0;
		virtual void ReclaimCompleted() noexcept = 0;

		virtual void CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
			uint64_t srcOffset, uint64_t numBytes) noexcept = 0;
		[[nodiscard]] virtual bool UploadBuffer(const void* data, uint64_t sizeInBytes,
			RHIBufferHandle dst, uint64_t dstOffset = 0) noexcept = 0;
		[[nodiscard]] virtual bool UploadTexture(
			const RHITextureUploadData& uploadData, RHITextureHandle dst) noexcept = 0;
		[[nodiscard]] virtual RHITextureReadbackRequest ReadbackTexture(
			RHITextureHandle src, const RHITextureDesc& desc) noexcept = 0;
	};
}
