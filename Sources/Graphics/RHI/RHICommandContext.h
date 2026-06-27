#pragma once
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIPipeline.h"
#include "Graphics/RHI/RHITexture.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

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
		RHIDescriptorHeapType m_HeapType = RHIDescriptorHeapType::CbvSrvUav;
	};

	struct RHIViewport
	{
		float m_X = 0.0f;
		float m_Y = 0.0f;
		float m_Width = 0.0f;
		float m_Height = 0.0f;
		float m_MinDepth = 0.0f;
		float m_MaxDepth = 1.0f;
	};

	struct RHIScissorRect
	{
		int32_t m_Left = 0;
		int32_t m_Top = 0;
		int32_t m_Right = 0;
		int32_t m_Bottom = 0;
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
		virtual void SetRenderTargets(
			std::span<const RHITextureViewHandle> renderTargets,
			RHITextureViewHandle depthStencil = {}) noexcept = 0;
		virtual void ClearColor(
			RHITextureViewHandle renderTarget,
			const std::array<float, 4>& color) noexcept = 0;
		virtual void ClearDepthStencil(
			RHITextureViewHandle depthStencil,
			float depth,
			std::optional<uint8_t> stencil = std::nullopt) noexcept = 0;
		virtual void SetViewport(const RHIViewport& viewport) noexcept = 0;
		virtual void SetScissorRect(const RHIScissorRect& rect) noexcept = 0;
		virtual void SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept = 0;
		virtual void SetConstantBuffer(
			uint32_t parameterIndex,
			RHIBufferHandle buffer,
			uint64_t offset = 0) noexcept = 0;
		virtual void SetReadOnlyBuffer(
			uint32_t parameterIndex,
			RHIBufferHandle buffer,
			uint64_t offset = 0) noexcept = 0;
		virtual void SetPushConstants(
			uint32_t parameterIndex,
			std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept = 0;

		template<typename T>
		void SetPushConstants(uint32_t parameterIndex, const T& values, uint32_t destOffset = 0) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			static_assert(std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);

			std::array<uint32_t, sizeof(T) / sizeof(uint32_t)> data{};
			std::memcpy(data.data(), &values, sizeof(T));
			SetPushConstants(parameterIndex, std::span<const uint32_t>(data), destOffset);
		}

		virtual void SetVertexBuffers(uint32_t startSlot,
			std::span<const RHIVertexBufferBinding> bindings) noexcept = 0;
		virtual void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept = 0;
		virtual void DrawIndexed(uint32_t indexCount,
			uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0,
			int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept = 0;
		virtual void Draw(uint32_t vertexCount,
			uint32_t instanceCount = 1,
			uint32_t startVertexLocation = 0,
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
