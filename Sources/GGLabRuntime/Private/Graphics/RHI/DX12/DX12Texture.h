#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabFoundation/Platform/Win/ComTypes.h"
#include "Graphics/RHI/DX12/DX12Resource.h"

namespace gglab
{
	class DX12Texture : public DX12Resource
	{
	public:
		DX12Texture() noexcept = default;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12Texture);
		~DX12Texture() override = default;
	};
}
