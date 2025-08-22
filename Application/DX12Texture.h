#pragma once
#include "DX12Resource.h"

namespace graphicsGadgetLab
{
	class DX12Texture : public DX12Resource
	{
	public:
		DX12Texture() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Texture);
		virtual ~DX12Texture() = default;

		void AdoptFromSwapChain(ComPtr<ID3D12Resource> backBuffer) noexcept;

		static CreateInfo AssetTextureCreateInfo(D3D12MA::Allocator* allocator, CD3DX12_RESOURCE_DESC resourceDesc) noexcept;
	};
}