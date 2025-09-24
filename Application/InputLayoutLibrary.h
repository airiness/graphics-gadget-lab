#pragma once

namespace gglab
{
	enum class InputLayoutId : uint32_t
	{
		P3,				// Position(3)
		P3T2,			// Position(3), TexCoord(2)
		P3N3,			// Position(3), Normal(3)
		P3N3T2,			// Position(3), Normal(3), TexCoord(2)

		Count
	};

	class InputLayoutLibrary
	{
	public:
		[[nodiscard]] static D3D12_INPUT_LAYOUT_DESC Get(InputLayoutId id) noexcept;
	};
}