#include "Core/Precompiled.h"
#include "Graphics/TransferBatch.h"

namespace gglab
{
	namespace
	{
		constexpr RHIResourceState CopyDestState{
			.m_Stages = RHIStage::Copy,
			.m_Access = RHIAccess::CopyDest,
			.m_Layout = RHILayout::CopyDest,
		};

		constexpr RHIResourceState CommonState{
			.m_Stages = RHIStage::All,
			.m_Access = RHIAccess::Common,
			.m_Layout = RHILayout::Common,
		};
	}

	TransferBatch::TransferBatch(RHITransferContext& transferContext) noexcept :
		m_TransferContext(&transferContext)
	{
	}

	TransferBatch::TransferBatch(TransferBatch&& other) noexcept :
		m_TransferContext(std::exchange(other.m_TransferContext, nullptr)),
		m_Publications(std::move(other.m_Publications))
	{
	}

	TransferBatch& TransferBatch::operator=(TransferBatch&& other) noexcept
	{
		if (this != &other)
		{
			AbortIfActive();
			m_TransferContext = std::exchange(other.m_TransferContext, nullptr);
			m_Publications = std::move(other.m_Publications);
		}
		return *this;
	}

	TransferBatch::~TransferBatch()
	{
		AbortIfActive();
	}

	bool TransferBatch::UploadBuffer(
		RHIBufferHandle dstBuffer, uint64_t dstOffset, const void* src, uint64_t numBytes) noexcept
	{
		if (!dstBuffer.IsValid() || !src || numBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("TransferBatch rejected an invalid RHI buffer write.");
			return false;
		}

		if (!m_TransferContext->UploadBuffer(src, numBytes, dstBuffer, dstOffset))
		{
			return false;
		}
		return PublishBuffer(dstBuffer, CommonState);
	}

	bool TransferBatch::UploadTexture(RHITextureHandle dstTexture,
		const RHITextureUploadData& uploadData, RHIResourceState initialState,
		RHIResourceState publishedState,
		std::optional<RHISubresourceRange> subresources) noexcept
	{
		if (!dstTexture.IsValid() ||
			!IsRHIResourceStateValid(initialState, RHIResourceStateUsage::TextureInitial) ||
			!IsRHIResourceStateValid(publishedState, RHIResourceStateUsage::TextureBarrierAfter))
		{
			GGLAB_LOG_GRAPHICS_WARN("TransferBatch rejected invalid texture publication states.");
			return false;
		}

		if (initialState != CopyDestState)
		{
			const RHITextureBarrier barrier{
				.m_Texture = dstTexture,
				.m_Before = initialState,
				.m_After = CopyDestState,
				.m_Subresources = subresources,
			};
			m_TransferContext->TextureBarrier(std::span{ &barrier, 1 });
		}
		if (!m_TransferContext->UploadTexture(uploadData, dstTexture))
		{
			return false;
		}
		if (publishedState != CopyDestState)
		{
			const RHITextureBarrier barrier{
				.m_Texture = dstTexture,
				.m_Before = CopyDestState,
				.m_After = publishedState,
				.m_Subresources = subresources,
			};
			m_TransferContext->TextureBarrier(std::span{ &barrier, 1 });
		}
		return PublishTexture(dstTexture, publishedState, subresources);
	}

	RHITextureReadbackRequest TransferBatch::ReadbackTexture(
		RHITextureHandle srcTexture, const RHITextureDesc& desc) noexcept
	{
		RHITextureReadbackRequest request = m_TransferContext->ReadbackTexture(srcTexture, desc);
		if (request.IsValid())
		{
			const RHIResourceState copySourceState{
				.m_Stages = RHIStage::Copy,
				.m_Access = RHIAccess::CopySource,
				.m_Layout = RHILayout::CopySource,
			};
			PublishTexture(srcTexture, copySourceState);
			PublishBuffer(request.m_Buffer.Get(), CopyDestState);
		}
		return request;
	}

	void TransferBatch::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
		uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		m_TransferContext->CopyBuffer(dst, dstOffset, src, srcOffset, numBytes);
	}

	void TransferBatch::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		m_TransferContext->TextureBarrier(barriers);
	}

	void TransferBatch::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		m_TransferContext->BufferBarrier(barriers);
	}

	bool TransferBatch::PublishTexture(RHITextureHandle texture, RHIResourceState publishedState,
		std::optional<RHISubresourceRange> subresources) noexcept
	{
		if (!texture.IsValid() || !IsRHIResourceStateValid(
			publishedState, RHIResourceStateUsage::TextureBarrierAfter))
		{
			return false;
		}
		auto existing = std::ranges::find_if(m_Publications,
			[&](const RHITransferResourcePublication& publication)
			{
				return publication.m_Type == RHIResourceType::Texture &&
					publication.m_Texture == texture && publication.m_Subresources == subresources;
			});
		if (existing != m_Publications.end())
		{
			existing->m_PublishedState = publishedState;
			return true;
		}
		m_Publications.push_back({
			.m_Type = RHIResourceType::Texture,
			.m_Texture = texture,
			.m_Subresources = subresources,
			.m_PublishedState = publishedState,
			});
		return true;
	}

	bool TransferBatch::PublishBuffer(
		RHIBufferHandle buffer, RHIResourceState publishedState) noexcept
	{
		if (!buffer.IsValid() ||
			!IsRHIResourceStateValid(publishedState, RHIResourceStateUsage::Buffer))
		{
			return false;
		}
		auto existing = std::ranges::find_if(m_Publications,
			[&](const RHITransferResourcePublication& publication)
			{
				return publication.m_Type == RHIResourceType::Buffer &&
					publication.m_Buffer == buffer;
			});
		if (existing != m_Publications.end())
		{
			existing->m_PublishedState = publishedState;
			return true;
		}
		m_Publications.push_back({
			.m_Type = RHIResourceType::Buffer,
			.m_Buffer = buffer,
			.m_PublishedState = publishedState,
			});
		return true;
	}

	RHITransferSubmission TransferBatch::Submit(bool wait) noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_TransferContext);
		RHITransferContext* transferContext = std::exchange(m_TransferContext, nullptr);
		return {
			.m_Completion = transferContext->Submit(wait),
			.m_Publications = std::move(m_Publications),
		};
	}

	void TransferBatch::AbortIfActive() noexcept
	{
		if (m_TransferContext)
		{
			RHITransferContext* transferContext = std::exchange(m_TransferContext, nullptr);
			transferContext->Abort();
			m_Publications.clear();
		}
	}
}
