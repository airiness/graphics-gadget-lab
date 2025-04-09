#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	struct DX12RootSignatureDesc
	{
		CD3DX12_ROOT_SIGNATURE_DESC m_Desc;
	};

	class DX12RootSignature
	{
	public:
		explicit DX12RootSignature(DX12Device* dx12Device) noexcept;
		~DX12RootSignature() noexcept;

		bool IsValid() const noexcept { return m_RootSignature != nullptr; }
		void CreateRootSignature(const D3D12_ROOT_SIGNATURE_DESC& desc) noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;
		ComPtr<ID3D12RootSignature> m_RootSignature;
	};
}