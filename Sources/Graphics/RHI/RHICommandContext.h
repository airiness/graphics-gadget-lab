#pragma once
#include "Core/CoreMacros.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RHI/RHIFormat.h"
#include "Graphics/RHI/RHIPipeline.h"
#include "Graphics/RHI/RHITexture.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace gglab
{
	class RHICommandContext;

	[[nodiscard]] inline RHICommandContextHandle AllocateRHICommandContextHandle() noexcept
	{
		static std::atomic<RHICommandContextHandle::IndexType> nextIndex = 0;
		return RHICommandContextHandle(nextIndex.fetch_add(1, std::memory_order_relaxed), 1);
	}

	struct RHITextureBarrier
	{
		RHITextureHandle m_Texture{};
		RHIResourceState m_Before{};
		RHIResourceState m_After{};
		std::optional<RHISubresourceRange> m_Subresources = std::nullopt;
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

		bool operator==(const RHIVertexBufferBinding&) const noexcept = default;
	};

	struct RHIIndexBufferBinding
	{
		RHIBufferHandle m_Buffer{};
		uint64_t m_Offset = 0;
		uint32_t m_SizeInBytes = 0; // Logical range beginning at m_Offset.
		RHIFormat m_Format = RHIFormat::Unknown;

		bool operator==(const RHIIndexBufferBinding&) const noexcept = default;
	};

	[[nodiscard]] constexpr inline bool IsRHIIndexBufferOffsetAligned(
		RHIFormat format, uint64_t offset) noexcept
	{
		return format == RHIFormat::R32Uint &&
			offset % GetRHIFormatInfo(format).m_BytesPerBlock == 0;
	}

	[[nodiscard]] constexpr inline bool IsRHIIndexBufferBindingRangeValid(
		const RHIIndexBufferBinding& binding, uint64_t bufferSizeInBytes) noexcept
	{
		if (!IsRHIIndexBufferOffsetAligned(binding.m_Format, binding.m_Offset))
		{
			return false;
		}
		const uint64_t indexSizeInBytes = GetRHIFormatInfo(binding.m_Format).m_BytesPerBlock;
		return binding.m_SizeInBytes != 0 && binding.m_SizeInBytes % indexSizeInBytes == 0 &&
			binding.m_Offset <= bufferSizeInBytes &&
			binding.m_SizeInBytes <= bufferSizeInBytes - binding.m_Offset;
	}

	[[nodiscard]] constexpr inline bool IsRHIIndexBufferDrawRangeValid(
		const RHIIndexBufferBinding& binding, uint32_t indexCount,
		uint32_t startIndexLocation) noexcept
	{
		if (binding.m_Format != RHIFormat::R32Uint)
		{
			return false;
		}
		const uint64_t indexSizeInBytes = GetRHIFormatInfo(binding.m_Format).m_BytesPerBlock;
		if (binding.m_SizeInBytes == 0 || binding.m_SizeInBytes % indexSizeInBytes != 0)
		{
			return false;
		}
		const uint64_t startInBytes = uint64_t{ startIndexLocation } * indexSizeInBytes;
		const uint64_t drawSizeInBytes = uint64_t{ indexCount } * indexSizeInBytes;
		return startInBytes <= binding.m_SizeInBytes &&
			drawSizeInBytes <= binding.m_SizeInBytes - startInBytes;
	}

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

		bool operator==(const RHIViewport&) const noexcept = default;
	};

	struct RHIScissorRect
	{
		int32_t m_Left = 0;
		int32_t m_Top = 0;
		int32_t m_Right = 0;
		int32_t m_Bottom = 0;

		bool operator==(const RHIScissorRect&) const noexcept = default;
	};

	enum class RHIContentLoadOp : uint8_t
	{
		Load,
		DontCare,
	};

	struct RHIRenderingAttachment
	{
		RHITextureViewHandle m_View{};
		RHIContentLoadOp m_LoadOp = RHIContentLoadOp::Load;
	};

	struct RHIRenderingInfo
	{
		std::span<const RHIRenderingAttachment> m_ColorAttachments{};
		std::optional<RHIRenderingAttachment> m_DepthAttachment = std::nullopt;
	};

	struct RHIRenderingSignature
	{
		std::array<RHIFormat, RHIGraphicsPipelineDesc::MaxRenderTargets> m_ColorFormats{};
		uint32_t m_ColorAttachmentCount = 0;
		RHIFormat m_DepthFormat = RHIFormat::Unknown;
		uint32_t m_SampleCount = 0;

		bool operator==(const RHIRenderingSignature&) const noexcept = default;
	};

	[[nodiscard]] constexpr inline bool IsGraphicsPipelineCompatible(
		const RHIGraphicsPipelineDesc& pipeline, const RHIRenderingSignature& rendering) noexcept
	{
		if (pipeline.m_RenderTargetCount != rendering.m_ColorAttachmentCount ||
			pipeline.m_DepthStencilFormat != rendering.m_DepthFormat ||
			pipeline.m_SampleCount != rendering.m_SampleCount)
		{
			return false;
		}
		for (uint32_t index = 0; index < rendering.m_ColorAttachmentCount; ++index)
		{
			if (pipeline.m_RenderTargetFormats[index] != rendering.m_ColorFormats[index])
			{
				return false;
			}
		}
		return true;
	}

	class RHICommandContext
	{
	public:
		virtual ~RHICommandContext() = default;

		virtual RHICommandContextHandle GetHandle() const noexcept = 0;
		virtual RHIQueueType GetQueueType() const noexcept = 0;
		virtual void TrackTextureUse(RHITextureHandle texture) noexcept = 0;
		virtual void TrackBufferUse(RHIBufferHandle buffer) noexcept = 0;
		// Barrier calls may enqueue backend-native groups. Flush once after the
		// complete synchronization boundary has been recorded.
		virtual void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept = 0;
		virtual void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept = 0;
		virtual void FlushBarriers() noexcept = 0;
		virtual void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept = 0;
		virtual void BeginGpuProfileScope(std::string_view name) noexcept { GGLAB_UNUSED(name); }
		virtual void EndGpuProfileScope() noexcept {}
	};

	class RHIGraphicsCommandContext : public RHICommandContext
	{
	public:
		~RHIGraphicsCommandContext() override = default;

		virtual void SetPipeline(RHIPipelineHandle pipeline) noexcept = 0;
		virtual void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept = 0;
		// A graphics pass may end rendering explicitly to open multiple rendering scopes;
		// otherwise RGExecutor closes the final active scope at the pass boundary.
		virtual void BeginRendering(const RHIRenderingInfo& info) noexcept = 0;
		virtual void EndRendering() noexcept = 0;
		[[nodiscard]] virtual bool IsRendering() const noexcept = 0;
		virtual void ClearColorAttachment(
			uint32_t colorAttachmentIndex, const std::array<float, 4>& color) noexcept = 0;
		virtual void ClearDepthAttachment(float depth,
			std::optional<uint8_t> stencil = std::nullopt) noexcept = 0;
		virtual void SetViewport(const RHIViewport& viewport) noexcept = 0;
		virtual void SetScissorRect(const RHIScissorRect& rect) noexcept = 0;
		virtual void SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept = 0;
		virtual void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept = 0;
		virtual void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept = 0;
		virtual void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept = 0;

		template <typename T>
		void SetPushConstants(
			uint32_t parameterIndex, const T& values, uint32_t destOffset = 0) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			static_assert(std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);

			std::array<uint32_t, sizeof(T) / sizeof(uint32_t)> data{};
			std::memcpy(data.data(), &values, sizeof(T));
			SetPushConstants(parameterIndex, std::span<const uint32_t>(data), destOffset);
		}

		virtual void SetVertexBuffers(
			uint32_t startSlot, std::span<const RHIVertexBufferBinding> bindings) noexcept = 0;
		virtual void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept = 0;
		virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept = 0;
		virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
			uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) noexcept = 0;

		void DrawFullscreenTriangle() noexcept
		{
			SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
			Draw(3);
		}
	};

	class RHIComputeCommandContext : public RHICommandContext
	{
	public:
		~RHIComputeCommandContext() override = default;

		virtual void SetPipeline(RHIPipelineHandle pipeline) noexcept = 0;
		virtual void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept = 0;
		virtual void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept = 0;
		virtual void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept = 0;
		virtual void SetReadWriteBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept = 0;
		virtual void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept = 0;

		template <typename T>
		void SetPushConstants(
			uint32_t parameterIndex, const T& values, uint32_t destOffset = 0) noexcept
		{
			static_assert(std::is_trivially_copyable_v<T>);
			static_assert(std::is_standard_layout_v<T>);
			static_assert(sizeof(T) % sizeof(uint32_t) == 0);

			std::array<uint32_t, sizeof(T) / sizeof(uint32_t)> data{};
			std::memcpy(data.data(), &values, sizeof(T));
			SetPushConstants(parameterIndex, std::span<const uint32_t>(data), destOffset);
		}

		virtual void Dispatch(
			uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept = 0;
	};
}
