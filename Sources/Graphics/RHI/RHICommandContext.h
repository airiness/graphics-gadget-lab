#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHITexture.h"

#include <cstdint>
#include <span>

namespace gglab
{
	class RHICommandContext;

	struct RHITextureBarrier
	{
		RHITextureHandle m_Texture{};
		RHIResourceState m_Before{};
		RHIResourceState m_After{};
	};

	struct RHIBufferBarrier
	{
		RHIBufferHandle m_Buffer{};
		RHIResourceState m_Before{};
		RHIResourceState m_After{};
	};

	struct RHISubmitInfo
	{
		RHIQueueType m_QueueType = RHIQueueType::Graphics;
		std::span<RHICommandContext* const> m_Contexts{};
		bool m_WaitForCompletion = false;
	};

	struct RHIVertexBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_Offset = 0;
		uint32_t m_Stride = 0;
		uint32_t m_SizeInBytes = 0;
	};

	struct RHIIndexBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_Offset = 0;
		uint32_t m_SizeInBytes = 0;
		RHIFormat m_Format = RHIFormat::Unknown;
	};

	struct RHIDescriptorTableBinding
	{
		uint32_t m_ParameterIndex = 0;
		uint32_t m_TableIndex = 0;
	};

	class RHICommandContext
	{
	public:
		virtual ~RHICommandContext() = default;

		virtual RHICommandContextHandle GetHandle() const noexcept = 0;
		virtual RHIQueueType GetQueueType() const noexcept = 0;
		virtual void TrackTextureUse(RHITextureHandle texture) noexcept = 0;
		virtual void TrackBufferUse(RHIBufferHandle buffer) noexcept = 0;
		virtual void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept = 0;
		virtual void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept = 0;
	};

	class RHIGraphicsCommandContext : public RHICommandContext
	{
	public:
		~RHIGraphicsCommandContext() override = default;

		virtual void SetPipeline(RHIPipelineHandle pipeline) noexcept = 0;
		virtual void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept = 0;
		virtual void SetVertexBuffers(uint32_t startSlot,
			std::span<const RHIVertexBufferBinding> bindings) noexcept = 0;
		virtual void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept = 0;
		virtual void DrawIndexed(uint32_t indexCount,
			uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0,
			int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept = 0;
	};

	class RHIComputeCommandContext : public RHICommandContext
	{
	public:
		~RHIComputeCommandContext() override = default;

		virtual void SetPipeline(RHIPipelineHandle pipeline) noexcept = 0;
		virtual void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept = 0;
		virtual void Dispatch(uint32_t groupCountX,
			uint32_t groupCountY,
			uint32_t groupCountZ) noexcept = 0;
	};
}
