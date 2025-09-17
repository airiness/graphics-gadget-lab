#pragma once
#include "FNV1a.h"
#include "TypedIndex.h"

namespace gglab
{


	


	class DX12Device;
	class DX12PSOCache
	{
	public:
		explicit DX12PSOCache(DX12Device* dx12Device) noexcept;
		GGLAB_DELETE_COPYABLE_DEFAULT_MOVABLE(DX12PSOCache);
		~DX12PSOCache();
	};
}