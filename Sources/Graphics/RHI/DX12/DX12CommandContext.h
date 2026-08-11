#pragma once
#include "Core/CoreMacros.h"
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/Descriptor/DX12DescriptorTypes.h"
#include "Graphics/RHI/RHICommandContext.h"

#include <memory>
#include <vector>

namespace gglab
{
	class DX12CommandList;
	class DX12Device;
	class DX12PipelineState;
	class DX12PipelineSystem;
	class DX12RootSignature;
	class DX12GpuProfiler;

	class DX12CommandContext
	{
	public:
		DX12CommandContext(
			DX12Device* device, DX12CommandList* commandList, RHIQueueType queueType) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12CommandContext);
		virtual ~DX12CommandContext() = default;

		[[nodiscard]] RHICommandContextHandle GetHandle() const noexcept { return m_Handle; }
		[[nodiscard]] RHIQueueType GetQueueType() const noexcept { return m_QueueType; }
		[[nodiscard]] DX12CommandList* GetCommandList() const noexcept { return m_CommandList; }
		[[nodiscard]] ID3D12GraphicsCommandList* Get() const noexcept;

		[[nodiscard]] std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept
		{
			return m_UsedBuffers;
		}
		[[nodiscard]] std::span<const RHITextureHandle> GetUsedTextures() const noexcept
		{
			return m_UsedTextures;
		}
		void ClearTrackedResourceUses() noexcept;
		void TrackBufferUse(RHIBufferHandle buffer) noexcept;
		void TrackTextureUse(RHITextureHandle texture) noexcept;
		void BeginRenderingScope() noexcept;
		void EndRenderingScope() noexcept;
		[[nodiscard]] bool IsRendering() const noexcept { return m_IsRendering; }

		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept;
		void FlushBarriers() noexcept;
		void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept;

	protected:
		[[nodiscard]] DX12Device* GetDevice() const noexcept { return m_Device; }

	private:
		friend class DX12GraphicsCommandContext;
		friend class DX12ComputeCommandContext;

		RHICommandContextHandle m_Handle{};
		DX12Device* m_Device = nullptr;
		DX12CommandList* m_CommandList = nullptr;
		RHIQueueType m_QueueType = RHIQueueType::Graphics;
		bool m_IsRendering = false;
		std::vector<RHIBufferHandle> m_UsedBuffers;
		std::vector<RHITextureHandle> m_UsedTextures;
	};

	class DX12GraphicsCommandContext final : public RHIGraphicsCommandContext
	{
	public:
		DX12GraphicsCommandContext(DX12Device* device, DX12PipelineSystem* pipelineSystem,
			DX12CommandList* commandList) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12GraphicsCommandContext);
		~DX12GraphicsCommandContext() override = default;

		RHICommandContextHandle GetHandle() const noexcept override
		{
			return m_Backend.GetHandle();
		}
		RHIQueueType GetQueueType() const noexcept override { return m_Backend.GetQueueType(); }
		DX12CommandList* GetCommandList() const noexcept { return m_Backend.GetCommandList(); }
		ID3D12GraphicsCommandList* Get() const noexcept { return m_Backend.Get(); }

		std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept
		{
			return m_Backend.GetUsedBuffers();
		}
		std::span<const RHITextureHandle> GetUsedTextures() const noexcept
		{
			return m_Backend.GetUsedTextures();
		}
		void ClearTrackedResourceUses() noexcept
		{
			m_Backend.ClearTrackedResourceUses();
			m_IndexBufferBinding.reset();
			m_CurrentRootSignature = nullptr;
			m_CurrentPipelineDesc.reset();
			m_ActiveRenderingSignature.reset();
			m_ActiveColorAttachments.clear();
			m_ActiveDepthAttachment.reset();
		}

		void TrackTextureUse(RHITextureHandle texture) noexcept override
		{
			m_Backend.TrackTextureUse(texture);
		}
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override
		{
			m_Backend.TrackBufferUse(buffer);
		}
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void FlushBarriers() noexcept override { m_Backend.FlushBarriers(); }
		void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept override
		{
			m_Backend.CopyBuffer(destination, destinationOffset, source, sourceOffset, sizeInBytes);
		}
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void BeginRendering(const RHIRenderingInfo& info) noexcept override;
		void EndRendering() noexcept override;
		bool IsRendering() const noexcept override { return m_Backend.IsRendering(); }
		void ClearColorAttachment(
			uint32_t colorAttachmentIndex, const std::array<float, 4>& color) noexcept override;
		void ClearDepthAttachment(float depth,
			std::optional<uint8_t> stencil = std::nullopt) noexcept override;
		void SetViewport(const RHIViewport& viewport) noexcept override;
		void SetScissorRect(const RHIScissorRect& rect) noexcept override;
		void SetPrimitiveTopology(RHIPrimitiveTopology topology) noexcept override;
		void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept override;
		void SetVertexBuffers(
			uint32_t startSlot, std::span<const RHIVertexBufferBinding> bindings) noexcept override;
		void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept override;
		void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0, int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept override;
		void Draw(uint32_t vertexCount, uint32_t instanceCount = 1,
			uint32_t startVertexLocation = 0, uint32_t startInstanceLocation = 0) noexcept override;
		void BeginGpuProfileScope(std::string_view name) noexcept override;
		void EndGpuProfileScope() noexcept override;
		void SetGpuProfiler(DX12GpuProfiler* profiler) noexcept { m_GpuProfiler = profiler; }

		void SetRootSignature(const DX12RootSignature& rootSignature) noexcept;
		void SetPipelineState(const DX12PipelineState& pipelineState) noexcept;
		void SetDescriptor(uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept;

	private:
		friend class DX12ComputeCommandContext;
		[[nodiscard]] bool ValidateActivePipelineCompatibility(std::string_view operation) const noexcept;

		DX12CommandContext m_Backend;
		DX12PipelineSystem* m_PipelineSystem = nullptr;
		DX12RootSignature* m_CurrentRootSignature = nullptr;
		std::optional<RHIGraphicsPipelineDesc> m_CurrentPipelineDesc;
		std::optional<RHIRenderingSignature> m_ActiveRenderingSignature;
		std::vector<DX12DescriptorView> m_ActiveColorAttachments;
		std::optional<DX12DescriptorView> m_ActiveDepthAttachment;
		std::optional<RHIIndexBufferBinding> m_IndexBufferBinding;
		DX12GpuProfiler* m_GpuProfiler = nullptr;
	};

	class DX12ComputeCommandContext final : public RHIComputeCommandContext
	{
	public:
		DX12ComputeCommandContext(DX12Device* device, DX12PipelineSystem* pipelineSystem,
			DX12CommandList* commandList) noexcept;
		DX12ComputeCommandContext(DX12GraphicsCommandContext& graphicsContext,
			DX12PipelineSystem* pipelineSystem) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ComputeCommandContext);
		~DX12ComputeCommandContext() override = default;

		RHICommandContextHandle GetHandle() const noexcept override
		{
			return m_Backend->GetHandle();
		}
		RHIQueueType GetQueueType() const noexcept override { return m_Backend->GetQueueType(); }
		DX12CommandList* GetCommandList() const noexcept { return m_Backend->GetCommandList(); }
		ID3D12GraphicsCommandList* Get() const noexcept { return m_Backend->Get(); }

		std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept
		{
			return m_Backend->GetUsedBuffers();
		}
		std::span<const RHITextureHandle> GetUsedTextures() const noexcept
		{
			return m_Backend->GetUsedTextures();
		}
		void ClearTrackedResourceUses() noexcept
		{
			m_Backend->ClearTrackedResourceUses();
			m_CurrentRootSignature = nullptr;
		}
		void ResetEncoderState() noexcept { m_CurrentRootSignature = nullptr; }

		void TrackTextureUse(RHITextureHandle texture) noexcept override
		{
			m_Backend->TrackTextureUse(texture);
		}
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override
		{
			m_Backend->TrackBufferUse(buffer);
		}
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void FlushBarriers() noexcept override { m_Backend->FlushBarriers(); }
		void CopyBuffer(RHIBufferHandle destination, uint64_t destinationOffset,
			RHIBufferHandle source, uint64_t sourceOffset, uint64_t sizeInBytes) noexcept override
		{
			m_Backend->CopyBuffer(
				destination, destinationOffset, source, sourceOffset, sizeInBytes);
		}
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void SetConstantBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadOnlyBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetReadWriteBuffer(
			uint32_t parameterIndex, RHIBufferHandle buffer, uint64_t offset = 0) noexcept override;
		void SetPushConstants(uint32_t parameterIndex, std::span<const uint32_t> values,
			uint32_t destOffset = 0) noexcept override;
		void Dispatch(
			uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) noexcept override;
		void BeginGpuProfileScope(std::string_view name) noexcept override;
		void EndGpuProfileScope() noexcept override;
		void SetGpuProfiler(DX12GpuProfiler* profiler) noexcept { m_GpuProfiler = profiler; }

		void SetPipelineState(const DX12PipelineState& pipelineState) noexcept;
		void SetDescriptor(uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept;

	private:
		std::unique_ptr<DX12CommandContext> m_OwnedBackend;
		DX12CommandContext* m_Backend = nullptr;
		DX12PipelineSystem* m_PipelineSystem = nullptr;
		DX12RootSignature* m_CurrentRootSignature = nullptr;
		DX12GpuProfiler* m_GpuProfiler = nullptr;
	};
}
