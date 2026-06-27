#pragma once
#include "Core/CoreMacros.h"
#include "Core/Platform/Win/ComTypes.h"
#include "Graphics/RHI/RHICommandContext.h"

#include <vector>

namespace gglab
{
	struct DX12DescriptorView;
	class DX12CommandList;
	class DX12Device;
	class DX12PipelineState;
	class DX12RootSignature;

	[[nodiscard]] RHICommandContextHandle AllocateDX12CommandContextHandle() noexcept;

	class DX12CommandContext
	{
	public:
		DX12CommandContext(DX12Device* device,
			DX12CommandList* commandList,
			RHIQueueType queueType) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12CommandContext);
		virtual ~DX12CommandContext() = default;

		[[nodiscard]] RHICommandContextHandle GetHandle() const noexcept { return m_Handle; }
		[[nodiscard]] RHIQueueType GetQueueType() const noexcept { return m_QueueType; }
		[[nodiscard]] DX12CommandList* GetCommandList() const noexcept { return m_CommandList; }
		[[nodiscard]] ID3D12GraphicsCommandList* Get() const noexcept;

		[[nodiscard]] std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept { return m_UsedBuffers; }
		[[nodiscard]] std::span<const RHITextureHandle> GetUsedTextures() const noexcept { return m_UsedTextures; }
		void ClearTrackedResourceUses() noexcept;
		void TrackBufferUse(RHIBufferHandle buffer) noexcept;
		void TrackTextureUse(RHITextureHandle texture) noexcept;

		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept;

	protected:
		[[nodiscard]] DX12Device* GetDevice() const noexcept { return m_Device; }

	private:
		friend class DX12GraphicsCommandContext;
		friend class DX12ComputeCommandContext;

		RHICommandContextHandle m_Handle{};
		DX12Device* m_Device = nullptr;
		DX12CommandList* m_CommandList = nullptr;
		RHIQueueType m_QueueType = RHIQueueType::Graphics;
		std::vector<RHIBufferHandle> m_UsedBuffers;
		std::vector<RHITextureHandle> m_UsedTextures;
	};

	class DX12GraphicsCommandContext final : public RHIGraphicsCommandContext
	{
	public:
		DX12GraphicsCommandContext(DX12Device* device,
			DX12CommandList* commandList) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12GraphicsCommandContext);
		~DX12GraphicsCommandContext() override = default;

		RHICommandContextHandle GetHandle() const noexcept override { return m_Backend.GetHandle(); }
		RHIQueueType GetQueueType() const noexcept override { return m_Backend.GetQueueType(); }
		DX12CommandList* GetCommandList() const noexcept { return m_Backend.GetCommandList(); }
		ID3D12GraphicsCommandList* Get() const noexcept { return m_Backend.Get(); }

		std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept { return m_Backend.GetUsedBuffers(); }
		std::span<const RHITextureHandle> GetUsedTextures() const noexcept { return m_Backend.GetUsedTextures(); }
		void ClearTrackedResourceUses() noexcept { m_Backend.ClearTrackedResourceUses(); }

		void TrackTextureUse(RHITextureHandle texture) noexcept override { m_Backend.TrackTextureUse(texture); }
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override { m_Backend.TrackBufferUse(buffer); }
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void SetVertexBuffers(uint32_t startSlot,
			std::span<const RHIVertexBufferBinding> bindings) noexcept override;
		void SetIndexBuffer(const RHIIndexBufferBinding& binding) noexcept override;
		void DrawIndexed(uint32_t indexCount,
			uint32_t instanceCount = 1,
			uint32_t startIndexLocation = 0,
			int32_t baseVertexLocation = 0,
			uint32_t startInstanceLocation = 0) noexcept override;

		void SetRootSignature(const DX12RootSignature& rootSignature) noexcept;
		void SetPipelineState(const DX12PipelineState& pipelineState) noexcept;
		void SetDescriptor(uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept;

	private:
		DX12CommandContext m_Backend;
	};

	class DX12ComputeCommandContext final : public RHIComputeCommandContext
	{
	public:
		DX12ComputeCommandContext(DX12Device* device,
			DX12CommandList* commandList) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12ComputeCommandContext);
		~DX12ComputeCommandContext() override = default;

		RHICommandContextHandle GetHandle() const noexcept override { return m_Backend.GetHandle(); }
		RHIQueueType GetQueueType() const noexcept override { return m_Backend.GetQueueType(); }
		DX12CommandList* GetCommandList() const noexcept { return m_Backend.GetCommandList(); }
		ID3D12GraphicsCommandList* Get() const noexcept { return m_Backend.Get(); }

		std::span<const RHIBufferHandle> GetUsedBuffers() const noexcept { return m_Backend.GetUsedBuffers(); }
		std::span<const RHITextureHandle> GetUsedTextures() const noexcept { return m_Backend.GetUsedTextures(); }
		void ClearTrackedResourceUses() noexcept { m_Backend.ClearTrackedResourceUses(); }

		void TrackTextureUse(RHITextureHandle texture) noexcept override { m_Backend.TrackTextureUse(texture); }
		void TrackBufferUse(RHIBufferHandle buffer) noexcept override { m_Backend.TrackBufferUse(buffer); }
		void TextureBarrier(std::span<const RHITextureBarrier> barriers) noexcept override;
		void BufferBarrier(std::span<const RHIBufferBarrier> barriers) noexcept override;
		void SetPipeline(RHIPipelineHandle pipeline) noexcept override;
		void SetDescriptorTable(const RHIDescriptorTableBinding& binding) noexcept override;
		void Dispatch(uint32_t groupCountX,
			uint32_t groupCountY,
			uint32_t groupCountZ) noexcept override;

		void SetPipelineState(const DX12PipelineState& pipelineState) noexcept;
		void SetDescriptor(uint32_t parameterIndex, const DX12DescriptorView& descriptor) noexcept;

	private:
		DX12CommandContext m_Backend;
	};
}
