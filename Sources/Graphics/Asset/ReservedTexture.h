#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Core/Utility/TypeUtils.h"

namespace gglab
{
	enum class ReservedTextureIDIndex : uint32_t
	{
		BaseColorWhite,
		MissingTextureChecker,
		NormalFlat,
		DefaultMetallicRoughness,
		OcclusionWhite,
		EmissiveWhite,
		ErrorRed,
		UVTest,
		UVTestTexture1K,
		UVTestTexture4K,
		FallbackEnvironmentCubemap,

		Count,

		ReservedCount = 64u
	};
	static_assert(utils::ToIndex(ReservedTextureIDIndex::Count) <
		utils::ToIndex(ReservedTextureIDIndex::ReservedCount),
		"ReservedTextureIDIndex::Count must be less than ReservedTextureIDIndex::ReservedCount");

	inline constexpr TextureID::ValueType ReservedTextureCount =
		static_cast<TextureID::ValueType>(utils::ToIndex(ReservedTextureIDIndex::ReservedCount));

	constexpr TextureID ToTextureId(ReservedTextureIDIndex index) noexcept
	{
		return TextureID{ static_cast<TextureID::ValueType>(utils::ToIndex(index)) };
	}

	constexpr bool IsReservedTextureId(TextureID id) noexcept
	{
		return id.IsValid() && id.Value() < ReservedTextureCount;
	}
}
