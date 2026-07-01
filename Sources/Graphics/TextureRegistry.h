#pragma once
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/TextureAsset.h"

namespace gglab
{
	class AssetManager;
	class RHIDevice;
	class TransferBatch;
	class TransferManager;
	struct AssetSnapshot;

	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;

	enum class ReservedTextureIDIndex : uint32_t
	{
		BaseColorWhite,
		MissingTextureChecker,
		NormalFlat,
		DefaultMetallicRoughness,
		OcclusionWhite,
		EmissiveBlack,
		ErrorRed,
		UVTest,
		UVTestTexture1K,
		UVTestTexture4K,

		Count,

		ReservedCount = 64u
	};
	static_assert(utils::ToIndex(ReservedTextureIDIndex::Count) < utils::ToIndex(ReservedTextureIDIndex::ReservedCount),
		"ReservedTextureID::Count must be less than ReservedTextureID::ReservedCount");

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

	class TextureRegistry
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TransferManager* m_TransferManager = nullptr;
		};

		struct TextureUploadData
		{
			TextureID m_TextureId{};
			TextureSemantic m_Semantic = TextureSemantic::GenericColor;
			TextureAssetData m_TextureData;
			TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
		};

	private:
		struct TextureContainer
		{
			std::unordered_map<std::filesystem::path, TextureID> m_PathIDMap;
			std::unordered_map<TextureID, std::unique_ptr<Texture>> m_TextureIDMap;
		};

	public:
		explicit TextureRegistry(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureRegistry);
		~TextureRegistry() = default;

		void InitializeReservedTextures() noexcept;
		void Finalize(const RHIFencePoint& fencePoint) noexcept;

		TextureID LoadTexture(const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor) noexcept;

		Texture* GetTexture(TextureID textureId) noexcept;
		const Texture* GetTexture(TextureID textureId) const noexcept;

		uint32_t ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;

		TextureID CreateTexture(const std::filesystem::path& canonicalPath) noexcept;
		TextureID FindTexture(const std::filesystem::path& canonicalPath) const noexcept;

		TextureUploadData MakeTextureUploadData(TextureID textureId,
			const std::filesystem::path& canonicalPath, TextureSemantic semantic) noexcept;

		TextureUploadData MakeTextureUploadData(TextureID textureId,
			TextureAssetData&& textureData, TextureSemantic semantic) noexcept;

		[[nodiscard]] bool UploadTexture(
			const TextureUploadData& uploadData,
			TransferBatch& transferBatch) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
		friend class AssetManager;

		void CreateTextureEntry(TextureID id, const char* texName) noexcept;
		bool RemoveTexture(TextureID textureId) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;

		TextureIDCounter m_TextureIdCounter{ ReservedTextureCount };
		TextureContainer m_TextureContainer;
	};
}
