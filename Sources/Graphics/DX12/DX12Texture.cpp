#include "Core/Precompiled.h"
#include "Graphics/DX12/DX12Texture.h"

namespace gglab
{
	void DX12Texture::AdoptFromSwapChain(ComPtr<ID3D12Resource> backBuffer) noexcept
	{
		AdoptExternal(std::move(backBuffer), D3D12_RESOURCE_STATE_PRESENT);
	}

	DX12Resource::CreateInfo DX12Texture::AssetTextureCreateInfo(D3D12MA::Allocator* allocator, CD3DX12_RESOURCE_DESC resourceDesc) noexcept
	{
		DX12Resource::CreateInfo createInfo = {};
		createInfo.m_Allocator = allocator;
		createInfo.m_AllocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
		createInfo.m_AllocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
		createInfo.m_InitStates = D3D12_RESOURCE_STATE_COMMON;
		createInfo.m_ResourceDesc = resourceDesc;

		return createInfo;
	}
}