#include "Precompiled.h"
#include "ViewCache.h"
#include "DX12Texture.h"

namespace gglab
{
	ViewCache::ViewKey ViewCache::ViewKey::CreateRTVKey(uint32_t resourceIndex, 
		DX12Texture* texture, 
		const D3D12_RENDER_TARGET_VIEW_DESC& inDesc, 
		D3D12_RENDER_TARGET_VIEW_DESC& outDesc) noexcept
	{
		const auto& rtDesc = texture->GetDesc();
		outDesc = inDesc;

		if (outDesc.Format == DXGI_FORMAT_UNKNOWN)
		{

		}

		return ViewKey();
	}
}