#pragma once
#include "GGLabRuntime/Graphics/Asset/AssetContentFingerprint.h"
#include "GGLabRuntime/Graphics/Asset/TextureAssetViews.h"

#include <cstdint>

namespace gglab
{
	enum class EnvironmentTextureSourceType : uint8_t
	{
		Equirectangular,
		Cubemap,
	};

	struct EnvironmentTextureSource
	{
		TextureContentRef m_Content{};
		EnvironmentTextureSourceType m_Type = EnvironmentTextureSourceType::Equirectangular;
		AssetContentFingerprint m_ContentFingerprint{};

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Content.IsValid() && m_ContentFingerprint.IsValid();
		}
	};
}
