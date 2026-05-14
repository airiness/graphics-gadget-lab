#pragma once
#include "Graphics/GraphicsTypes.h"

namespace gglab
{

	class InputLayoutLibrary
	{
	public:
		[[nodiscard]] static D3D12_INPUT_LAYOUT_DESC Get(InputLayoutID id) noexcept;
	};
}