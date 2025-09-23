#pragma once
namespace gglab
{
	class DX12Device;
	class DX12RootSignature
	{
	public:
		explicit DX12RootSignature(DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept;
		~DX12RootSignature() noexcept;

		ID3D12RootSignature* Get() const noexcept { return m_RootSignature.Get(); }
	private:
		void CreateRootSignature() noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc;
		ComPtr<ID3D12RootSignature> m_RootSignature;
	};
}