#pragma once
#include "Core/Platform/Win/ComTypes.h"
namespace gglab
{
	class DX12Device;
	class DX12RootSignature
	{
	public:
		explicit DX12RootSignature(DX12Device* dx12Device, const CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC& desc) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12RootSignature);
		~DX12RootSignature() = default;

		ID3D12RootSignature* Get() const noexcept { return m_RootSignature.Get(); }
		auto GetDesc() const noexcept { return m_Desc; }

	private:
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC m_Desc;
		ComPtr<ID3D12RootSignature> m_RootSignature;
	};
}
