#pragma once
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/GraphicsTypes.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace gglab
{
	struct TextureContentRef
	{
		TextureID m_Id{};
		uint64_t m_Generation = 0;

		[[nodiscard]] bool IsValid() const noexcept { return m_Id.IsValid() && m_Generation != 0; }

		bool operator==(const TextureContentRef&) const noexcept = default;
	};

	// Non-owning render snapshot. Valid only for the current owner-thread frame
	// build after acquisition; resolve it again instead of caching it across frames.
	struct ResidentTextureResource
	{
		TextureContentRef m_Content{};
		RHITextureHandle m_Texture{};
		RHITextureDesc m_Desc{};
		uint32_t m_SrvIndex = 0;
	};

	// Copied inspection data used to keep diagnostics away from texture storage.
	struct TextureAssetReadInfo
	{
		TextureContentRef m_Content{};
		AssetLifecycle m_Lifecycle{};
		std::filesystem::path m_SourcePath;
		TextureImportSettings m_ImportSettings{};
		TextureSemantic m_Semantic = TextureSemantic::Unknown;
		StringID m_Name{};
		RHITextureHandle m_Texture{};
		std::string m_DebugName;
		ArtifactContentDigest m_ArtifactContentDigest{};
		SourceDigest m_SourceDigest{};
		DerivedDataKey m_DerivedDataKey{};
		bool m_IsUploaded = false;
		bool m_HasSrv = false;
		bool m_IsReserved = false;
		bool m_IsCpuArtifactCached = false;
		bool m_IsDerivedDataCached = false;
	};
}
