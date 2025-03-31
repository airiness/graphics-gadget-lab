#pragma once
namespace graphicsGadgetLab
{
	class DX12Device
	{
	public:
		DX12Device() noexcept;
		~DX12Device() noexcept;

		void Initialize() noexcept;
		void Update() noexcept;
		void Finalize() noexcept;

	private:
		void InitializeDxgiFactory() noexcept;
		void InitializeDxgiAdapter() noexcept;

	private:
		ComPtr<IDXGIFactory> m_DxgiFactory;
		ComPtr<IDXGIAdapter> m_DxgiAdapter;
		ComPtr<ID3D12Device> m_D3D12Device;	
	};
}