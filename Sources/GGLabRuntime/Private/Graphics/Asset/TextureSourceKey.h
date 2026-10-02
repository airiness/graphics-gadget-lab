#pragma once
#include "GGLabRuntime/Core/Hash/KeyHash.h"
#include "GGLabRuntime/Graphics/Asset/TextureImportTypes.h"

#include <filesystem>
#include <tuple>

namespace gglab
{
	// In-memory source lookup only; persisted artifacts use content-derived keys.
	struct TextureSourceKey
	{
		std::filesystem::path m_CanonicalPath;
		TextureImportSettings m_ImportSettings{};

		[[nodiscard]] auto AsTuple() const noexcept
		{
			return std::tuple{
				std::filesystem::hash_value(m_CanonicalPath),
				m_ImportSettings.m_Semantic,
				m_ImportSettings.m_MipPolicy,
			};
		}
		bool operator==(const TextureSourceKey&) const noexcept = default;
	};
	using TextureSourceKeyHash = KeyHash<TextureSourceKey>;
}
