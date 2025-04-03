#pragma once

namespace graphicsGadgetLab
{
	class DX12Device;
	class DX12CommandList
	{
	public:
		DX12CommandList() noexcept = default;
		~DX12CommandList() noexcept;

		DX12CommandList(const DX12CommandList&) = delete;
		DX12CommandList& operator=(const DX12CommandList&) = delete;

		DX12CommandList(DX12CommandList&&) = delete;
		DX12CommandList& operator=(DX12CommandList&&) = delete;

		void Initialize(DX12Device* dx12Device,
			D3D12_COMMAND_LIST_TYPE type) noexcept;

		void Finalize() noexcept;

		void Begin() noexcept;
		void End() noexcept;

		void Execute(ID3D12CommandQueue* commandQueue) noexcept;

		ID3D12GraphicsCommandList* Get() const noexcept { return m_D3D12GraphicsCommandList.Get(); }
		ID3D12CommandAllocator* GetCommandAllocator() const noexcept { return m_D3D12CommandAllocator.Get(); }


	private:
		void CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE type) noexcept;
		void CreateCommandList(D3D12_COMMAND_LIST_TYPE type) noexcept;

	private:
		DX12Device* m_DX12Device = nullptr;

		D3D12_COMMAND_LIST_TYPE m_Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

		ComPtr<ID3D12GraphicsCommandList> m_D3D12GraphicsCommandList;
		ComPtr<ID3D12CommandAllocator> m_D3D12CommandAllocator;

	};
}


