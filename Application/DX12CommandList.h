#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandQueue;
	class DX12RootSignature;
	class DX12Descriptor;
	class DX12DescriptorHeap;
	class DX12CommandList
	{
	public:
		explicit DX12CommandList(DX12Device* dx12Device,
			D3D12_COMMAND_LIST_TYPE type) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DX12CommandList);
		~DX12CommandList() noexcept;

		void Begin() noexcept;
		void End() noexcept;

		void Execute(const DX12CommandQueue& commandQueue) noexcept;

		ID3D12GraphicsCommandList* Get() const noexcept { return m_D3D12GraphicsCommandList.Get(); }
		ID3D12CommandAllocator* GetCommandAllocator() const noexcept { return m_D3D12CommandAllocator.Get(); }

		void SetGraphicsRootSignature(const DX12RootSignature& rootSignature) noexcept;
		void SetDescriptorHeap(const DX12DescriptorHeap& descriptorHeap) noexcept;
		void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) noexcept;
		void SetScissorRect(uint32_t left, uint32_t top, uint32_t width, uint32_t height) noexcept;
		void SetPrimitiveTopology(D3D12_PRIMITIVE_TOPOLOGY topology) noexcept;
		void SetRenderTargets(std::span<DX12Descriptor> rtDescriptors, DX12Descriptor* dsDescriptor) noexcept;
		void AddTextureBarrier(const CD3DX12_TEXTURE_BARRIER& textureBarrier) noexcept;
		void AddBufferBarrier(const CD3DX12_BUFFER_BARRIER& bufferBarrier) noexcept;
		void AddGlobalBarrier(const CD3DX12_GLOBAL_BARRIER& globalBarrier) noexcept;
		void FlushBarriers() noexcept;
		void ClearRenderTarget(DX12Descriptor rtDescriptor, const float* clearColor) noexcept;
		void ClearDepthStencil(DX12Descriptor dsDescriptor, float depthClearValue, std::optional<uint8_t> stencilClearValue = std::nullopt) noexcept;

	private:
		void CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type) noexcept;
		void CreateCommandList(D3D12_COMMAND_LIST_TYPE type) noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;

		D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ComPtr<ID3D12GraphicsCommandList7> m_D3D12GraphicsCommandList;
		ComPtr<ID3D12CommandAllocator> m_D3D12CommandAllocator;

		std::vector<CD3DX12_TEXTURE_BARRIER> m_TextureBarriers;
		std::vector<CD3DX12_BUFFER_BARRIER> m_BufferBarriers;
		std::vector<CD3DX12_GLOBAL_BARRIER> m_GlobalBarriers;

	};
}


