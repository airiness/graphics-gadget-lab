#pragma once
#include "DX12DescriptorTypes.h"

namespace gglab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12RootSignature;
	class DX12DescriptorHeap;
	class DX12CommandAllocator;
	class DX12PipelineState;
	class DX12CommandList
	{
	public:
		struct CreateInfo
		{
			DX12Device* m_DX12Device = nullptr;
			D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		};

	public:
		explicit DX12CommandList(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12CommandList);
		~DX12CommandList() = default;

		void Begin(DX12CommandAllocator* allocator) const noexcept;
		void End() noexcept;

		void Execute(const DX12CommandQueue& commandQueue) const noexcept;

		ID3D12GraphicsCommandList* Get() const noexcept { return m_D3D12GraphicsCommandList.Get(); }

		void SetGraphicsRootSignature(const DX12RootSignature& rootSignature) const noexcept;
		void SetPipelineState(const DX12PipelineState& pipelineState) const noexcept;
		void SetDescriptorHeap(const DX12DescriptorHeap& descriptorHeap) const noexcept;
		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) const noexcept;
		void SetScissorRect(uint32_t left, uint32_t top, uint32_t width, uint32_t height) const noexcept;
		void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) const noexcept;
		void SetRenderTargets(std::span<DX12DescriptorView> rtDescriptors, const DX12DescriptorHandle* dsDescriptor) const noexcept;
		void SetVertexBuffers(uint32_t startSlot, std::span<D3D12_VERTEX_BUFFER_VIEW> vertexBufferViews) const noexcept;
		void SetIndexBuffer(const D3D12_INDEX_BUFFER_VIEW& indexBufferView) const noexcept;
		void SetGraphicsConstantBuffer(uint32_t parameterIndex, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress) const noexcept;
		void SetGraphicsDescriptor(uint32_t parameterIndex, const DX12DescriptorHandle& descriptor) const noexcept;
		void AddTextureBarrier(const CD3DX12_TEXTURE_BARRIER& textureBarrier) noexcept;
		void AddBufferBarrier(const CD3DX12_BUFFER_BARRIER& bufferBarrier) noexcept;
		void AddGlobalBarrier(const CD3DX12_GLOBAL_BARRIER& globalBarrier) noexcept;
		void FlushBarriers() noexcept;
		void ClearRenderTarget(const DX12DescriptorHandle& rtDescriptor, const Color& clearColor) const noexcept;
		void ClearDepthStencil(const DX12DescriptorHandle& dsDescriptor, float depthClearValue, std::optional<uint8_t> stencilClearValue = std::nullopt) const noexcept;
		void DrawIndexedInstanced(uint32_t indexCount) const noexcept;
		void DrawInstanced(uint32_t vertexCount) const noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ComPtr<ID3D12GraphicsCommandList7> m_D3D12GraphicsCommandList;

		std::vector<CD3DX12_TEXTURE_BARRIER> m_TextureBarriers;
		std::vector<CD3DX12_BUFFER_BARRIER> m_BufferBarriers;
		std::vector<CD3DX12_GLOBAL_BARRIER> m_GlobalBarriers;

	};
}