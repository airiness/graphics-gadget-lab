#pragma once

#include "GGLabRuntime/Graphics/Asset/AssetToolingControlBase.h"

namespace gglab
{
	class AssetManager;

	class AssetToolingControl final : public AssetToolingControlBase
	{
	public:
		explicit AssetToolingControl(AssetManager& assets) noexcept : m_Assets(assets) {}
		[[nodiscard]] ModelLoadReceipt LoadModelAsync(const std::filesystem::path& path) noexcept override;
		[[nodiscard]] TextureLoadReceipt LoadTextureAsync(
			const std::filesystem::path& path, TextureSemantic semantic) noexcept override;
		void ClearModelImportArtifactCache() noexcept override;
		void ClearTextureArtifactCache() noexcept override;
		[[nodiscard]] bool ClearTextureDerivedDataCache() noexcept override;

	private:
		AssetManager& m_Assets;
	};
}
