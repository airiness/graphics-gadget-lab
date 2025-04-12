#pragma once
namespace graphicsGadgetLab
{
	class DX12Device;
	struct DX12RootSignatureDesc
	{
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc;
	};

	class DX12RootSignature
	{
	public:
		explicit DX12RootSignature(DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept;
		~DX12RootSignature() noexcept;

		bool IsValid() const noexcept { return m_RootSignature != nullptr; }
	private:
		void CreateRootSignature() noexcept;
	private:
		DX12Device* m_DX12Device = nullptr;
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc;
		ComPtr<ID3D12RootSignature> m_RootSignature;
	};
}