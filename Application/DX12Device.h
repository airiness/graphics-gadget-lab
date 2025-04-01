#pragma once
namespace graphicsGadgetLab
{
	class DX12Device
	{
	public:
		DX12Device() noexcept;
		~DX12Device() noexcept;

		ID3D12Device* Get() const noexcept { return m_D3D12Device.Get(); }

	private:
		void InitializeDXGIFactory() noexcept;
		void InitializeDXGIAdapter() noexcept;
		void InitializeD3D12Device() noexcept;

		void CheckFeatureSupport() noexcept;

	private:
		ComPtr<IDXGIFactory7> m_DxgiFactory;
		ComPtr<IDXGIAdapter> m_DxgiAdapter;
		ComPtr<ID3D12Device> m_D3D12Device;

		D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_12_0;

		// supported features
		bool m_RayTracingSupported = false;
		bool m_MeshShaderSupported = false;
	};
}