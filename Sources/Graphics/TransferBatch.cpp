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

		[[nodiscard]] bool RangesOverlapDimension(
			uint32_t lhsBase, uint32_t lhsCount, uint32_t rhsBase, uint32_t rhsCount) noexcept
		{
			if (lhsCount == 0 || rhsCount == 0)
			{
				return false;
			}
			const uint64_t lhsEnd = lhsCount == RHISubresourceRange::Remaining
				? std::numeric_limits<uint64_t>::max()
				: static_cast<uint64_t>(lhsBase) + lhsCount;
			const uint64_t rhsEnd = rhsCount == RHISubresourceRange::Remaining
				? std::numeric_limits<uint64_t>::max()
				: static_cast<uint64_t>(rhsBase) + rhsCount;
			return lhsBase < rhsEnd && rhsBase < lhsEnd;
		}

		[[nodiscard]] bool RangesOverlap(
			const RHISubresourceRange& lhs, const RHISubresourceRange& rhs) noexcept
		{
			if (!Test(lhs.m_Aspects, rhs.m_Aspects))
			{
				return false;
			}
			return RangesOverlapDimension(lhs.m_BaseMip, lhs.m_MipCount, rhs.m_BaseMip,
				rhs.m_MipCount) &&
				RangesOverlapDimension(lhs.m_BaseArraySlice, lhs.m_ArraySliceCount,
					rhs.m_BaseArraySlice, rhs.m_ArraySliceCount);
		}
	}

	TransferBatch::TransferBatch(RHITransferContext& transferContext) noexcept :
		m_TransferContext(&transferContext)
	{
	}

	TransferBatch::TransferBatch(TransferBatch&& other) noexcept :
		m_TransferContext(std::exchange(other.m_TransferContext, nullptr)),
		m_Publications(std::move(other.m_Publications)), m_Poisoned(other.m_Poisoned)
	{
	}

	TransferBatch& TransferBatch::operator=(TransferBatch&& other) noexcept
	{
		if (this != &other)
		{
			AbortIfActive();
			m_TransferContext = std::exchange(other.m_TransferContext, nullptr);
			m_Publications = std::move(other.m_Publications);
			m_Poisoned = other.m_Poisoned;
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
		if (m_Poisoned)
		{
			return false;
		}
		if (!dstBuffer.IsValid() || !src || numBytes == 0)
		{
			GGLAB_LOG_GRAPHICS_WARN("TransferBatch rejected an invalid RHI buffer write.");
			return false;
		}
		const RHIBufferBarrier toCopyDest{
			.m_Buffer = dstBuffer,
			.m_Before = CommonState,
			.m_After = CopyDestState,
		};
		m_TransferContext->BufferBarrier(std::span{ &toCopyDest, 1 });

		if (!m_TransferContext->UploadBuffer(src, numBytes, dstBuffer, dstOffset))
		{
			Fail();
			return false;
		}
		const RHIBufferBarrier toPublished{
			.m_Buffer = dstBuffer,
			.m_Before = CopyDestState,
			.m_After = CommonState,
		};
		m_TransferContext->BufferBarrier(std::span{ &toPublished, 1 });
		return PublishBuffer(dstBuffer, CommonState);
	}

	bool TransferBatch::UploadTexture(RHITextureHandle dstTexture,
		const RHITextureUploadData& uploadData, RHIResourceState initialState,
		RHIResourceState publishedState,
		std::optional<RHISubresourceRange> subresources) noexcept
	{
		if (m_Poisoned)
		{
			return false;
		}
		if (!dstTexture.IsValid() ||
			!IsRHIResourceStateValid(initialState, RHIResourceStateUsage::TextureInitial) ||
			!IsRHIResourceStateValid(publishedState, RHIResourceStateUsage::TextureBarrierAfter))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"TransferBatch rejected invalid texture publication states.");
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
			Fail();
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
		if (!PublishTexture(dstTexture, publishedState, subresources))
		{
			Fail();
			return false;
		}
		return true;
	}

	RHITextureReadbackRequest TransferBatch::ReadbackTexture(
		RHITextureHandle srcTexture, const RHITextureDesc& desc) noexcept
	{
		if (m_Poisoned)
		{
			return {};
		}
		const RHIResourceState copySourceState{
			.m_Stages = RHIStage::Copy,
			.m_Access = RHIAccess::CopySource,
			.m_Layout = RHILayout::CopySource,
		};
		const RHITextureBarrier toCopySource{
			.m_Texture = srcTexture,
			.m_Before = CommonState,
			.m_After = copySourceState,
		};
		m_TransferContext->TextureBarrier(std::span{ &toCopySource, 1 });
		RHITextureReadbackRequest request = m_TransferContext->ReadbackTexture(srcTexture, desc);
		if (!request.IsValid())
		{
			Fail();
			return {};
		}
		if (!PublishTexture(srcTexture, copySourceState) ||
			!PublishBuffer(request.m_Buffer.Get(), CopyDestState))
		{
			Fail();
			return {};
		}
		return request;
	}

	void TransferBatch::CopyBuffer(RHIBufferHandle dst, uint64_t dstOffset, RHIBufferHandle src,
		uint64_t srcOffset, uint64_t numBytes) noexcept
	{
		if (m_Poisoned)
		{
			return;
		}
		m_TransferContext->CopyBuffer(dst, dstOffset, src, srcOffset, numBytes);
	}

	void TransferBatch::TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept
	{
		if (m_Poisoned)
		{
			return;
		}
		m_TransferContext->TextureBarrier(barriers);
	}

	void TransferBatch::BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept
	{
		if (m_Poisoned)
		{
			return;
		}
		m_TransferContext->BufferBarrier(barriers);
	}

	bool TransferBatch::PublishTexture(RHITextureHandle texture, RHIResourceState publishedState,
		std::optional<RHISubresourceRange> subresources) noexcept
	{
		if (m_Poisoned)
		{
			return false;
		}
		if (!texture.IsValid() || !IsRHIResourceStateValid(
			publishedState, RHIResourceStateUsage::TextureBarrierAfter))
		{
			return false;
		}

		// Validate the candidate against the complete existing manifest
		// of this texture before mutating anything. An exact-range update must
		// also stay consistent with every other overlapping publication, so the
		// manifest can never end up with two terminal states for one subresource.
		const RHISubresourceRange effectiveRange = subresources.value_or(RHISubresourceRange{});
		std::optional<size_t> exactEntryIndex;
		for (size_t index = 0; index < m_Publications.size(); ++index)
		{
			const auto& publication = m_Publications[index];
			if (publication.m_Type != RHIResourceType::Texture ||
				publication.m_Texture != texture)
			{
				continue;
			}
			const RHISubresourceRange existingRange =
				publication.m_Subresources.value_or(RHISubresourceRange{});
			if (existingRange == effectiveRange)
			{
				exactEntryIndex = index;
				continue;
			}
			if (RangesOverlap(existingRange, effectiveRange) &&
				publication.m_PublishedState != publishedState)
			{
				GGLAB_LOG_GRAPHICS_ERROR(
					"TransferBatch rejected texture publications whose overlapping ranges would end with conflicting terminal states.");
				Fail();
				return false;
			}
		}

		// Mutate only after the candidate passed the complete check.
		if (exactEntryIndex)
		{
			m_Publications[*exactEntryIndex].m_PublishedState = publishedState;
		}
		else
		{
			m_Publications.push_back({
				.m_Type = RHIResourceType::Texture,
				.m_Texture = texture,
				.m_Subresources = subresources,
				.m_PublishedState = publishedState,
				});
		}
		return true;
	}

	bool TransferBatch::PublishBuffer(
		RHIBufferHandle buffer, RHIResourceState publishedState) noexcept
	{
		if (m_Poisoned)
		{
			return false;
		}
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
		if (m_Poisoned)
		{
			AbortIfActive();
			return {};
		}
		GGLAB_ASSERT_NOT_NULL(m_TransferContext);
		RHITransferContext* transferContext = std::exchange(m_TransferContext, nullptr);
		const RHIFencePoint completion = transferContext->Submit(wait);
		if (!completion.IsValid())
		{
			Fail();
			m_Publications.clear();
			return {};
		}
		return {
			.m_Completion = completion,
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
