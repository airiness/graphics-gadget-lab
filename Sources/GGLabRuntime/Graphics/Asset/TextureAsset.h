#pragma once
#include "Graphics/Asset/AssetContentFingerprint.h"
#include "Graphics/Asset/ArtifactContentDigest.h"
#include "Graphics/Asset/DerivedData/DerivedDataKey.h"
#include "Graphics/GraphicsTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t TextureDecoderVersion = 1;

	struct TextureAssetSubresource
	{
		uint64_t m_DataOffset = 0;
		uint64_t m_DataSize = 0;
		uint64_t m_RowPitch = 0;
		uint64_t m_SlicePitch = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Depth = 1;
		uint32_t m_MipLevel = 0;
		uint32_t m_ArraySlice = 0;
	};

	struct TextureAssetData
	{
		RHIFormat m_ResourceFormat = RHIFormat::Unknown;
		RHIFormat m_ViewFormat = RHIFormat::Unknown;
		RHITextureViewDimension m_SrvDimension = RHITextureViewDimension::Texture2D;
		RHIExtent3D m_Extent{};
		uint16_t m_ArraySize = 1;
		uint16_t m_MipLevels = 1;
		TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
		std::vector<std::byte> m_Pixels;
		std::vector<TextureAssetSubresource> m_Subresources;

		[[nodiscard]] bool IsValid() const noexcept;
		[[nodiscard]] RHITextureUploadData MakeUploadData() const noexcept;
	};

	// Mutable texture storage stays inside the asset subsystem. External consumers
	// receive generation-checked TextureContentRef/ResidentTextureResource views.
	struct TextureSourceInfo
	{
		std::filesystem::path m_CanonicalPath;
		TextureImportSettings m_ImportSettings{};
		AssetContentFingerprint m_ContentFingerprint{};
		ArtifactContentDigest m_ArtifactContentDigest{};
		SourceDigest m_SourceDigest{};
		DerivedDataKey m_DerivedDataKey{};
	};

	struct TextureGpuState
	{
		RHITextureHandle m_Texture;
		RHITextureViewHandle m_Srv{};
		RHITextureDesc m_Desc{};
		RHITextureViewDimension m_SrvDimension = RHITextureViewDimension::Unknown;
		bool m_IsUploaded = false;
	};

	struct TextureLoadState
	{
		ProgressChannelPtr m_Progress;
		bool m_CancelRequested = false;
		bool m_IsReloading = false;
	};

	struct Texture : AssetLifecycle
	{
		TextureID m_Id{};
		TextureSourceInfo m_Source{};
		StringID m_Name{};
		std::string m_DebugLabel;
		TextureGpuState m_Gpu{};
		TextureLoadState m_Load{};
	};

	[[nodiscard]] AssetContentFingerprint ComputeTextureContentFingerprint(
		const TextureAssetData& textureData, const TextureImportSettings& importSettings) noexcept;
}
