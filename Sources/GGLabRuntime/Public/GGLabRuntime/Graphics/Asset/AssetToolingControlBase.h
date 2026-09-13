#pragma once

#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "GGLabRuntime/Graphics/Asset/TextureImportTypes.h"

#include <cstdint>
#include <filesystem>

namespace gglab
{
	struct ModelLoadReceipt
	{
		ModelID m_ModelId{};
		uint64_t m_Generation = 0;
		uint64_t m_TaskId = 0;
		[[nodiscard]] bool IsValid() const noexcept { return m_ModelId.IsValid(); }
	};

	struct TextureLoadReceipt
	{
		TextureID m_TextureId{};
		uint64_t m_Generation = 0;
		uint64_t m_TaskId = 0;
		[[nodiscard]] bool IsValid() const noexcept { return m_TextureId.IsValid(); }
	};

	// Borrowed for synchronous tooling draw on the owner thread.
	// Receipts describe submission, not completion or residency, and own no task or asset.
	// Cache maintenance preserves the owner's existing retained-artifact and DDC policies.
	class AssetToolingControlBase
	{
	public:
		virtual ~AssetToolingControlBase() = default;
		[[nodiscard]] virtual ModelLoadReceipt LoadModelAsync(const std::filesystem::path& path) noexcept = 0;
		[[nodiscard]] virtual TextureLoadReceipt LoadTextureAsync(
			const std::filesystem::path& path, TextureSemantic semantic) noexcept = 0;
		virtual void ClearModelImportArtifactCache() noexcept = 0;
		virtual void ClearTextureArtifactCache() noexcept = 0;
		[[nodiscard]] virtual bool ClearTextureDerivedDataCache() noexcept = 0;
	};
}
