#include "Diagnostics/AssetToolingControl.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"

namespace gglab
{
	ModelLoadReceipt AssetToolingControl::LoadModelAsync(const std::filesystem::path& path) noexcept
	{
		const auto request = m_Assets.LoadModelAsync(path);
		return { request.m_ModelId, request.m_Generation, request.m_Task.m_Value };
	}

	TextureLoadReceipt AssetToolingControl::LoadTextureAsync(
		const std::filesystem::path& path, TextureSemantic semantic) noexcept
	{
		const auto request = m_Assets.LoadTextureAsync(path, semantic);
		return { request.m_TextureId, request.m_Generation, request.m_Task.m_Value };
	}

	void AssetToolingControl::ClearModelImportArtifactCache() noexcept
	{
		m_Assets.ClearModelImportArtifactCache();
	}

	void AssetToolingControl::ClearTextureArtifactCache() noexcept
	{
		m_Assets.ClearTextureArtifactCache();
	}

	bool AssetToolingControl::ClearTextureDerivedDataCache() noexcept
	{
		return m_Assets.ClearTextureDerivedDataCache();
	}
}
