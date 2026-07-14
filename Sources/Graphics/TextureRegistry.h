#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/TextureAsset.h"
#include "Graphics/TextureLoader.h"

namespace gglab
{
	class AssetManager;
	class AssetUploadScheduler;
	class RHIDevice;
	class TaskSystem;
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
		struct TextureCacheKey
		{
			std::filesystem::path m_CanonicalPath;
			TextureImportSettings m_ImportSettings{};

			bool operator==(const TextureCacheKey&) const noexcept = default;
		};

		struct TextureCacheKeyHash
		{
			size_t operator()(const TextureCacheKey& key) const noexcept
			{
				size_t hash = std::filesystem::hash_value(key.m_CanonicalPath);
				hash ^= static_cast<size_t>(key.m_ImportSettings.m_Semantic) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
				hash ^= static_cast<size_t>(key.m_ImportSettings.m_MipPolicy) +
					0x9e3779b9u + (hash << 6) + (hash >> 2);
				return hash;
			}
		};

	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			TransferManager* m_TransferManager = nullptr;
			AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
		};

		struct TextureLoadRequest
		{
			TextureID m_TextureId{};
			TaskHandle m_Task{};

			[[nodiscard]] bool IsValid() const noexcept { return m_TextureId.IsValid(); }
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
			std::unordered_map<TextureCacheKey, TextureID, TextureCacheKeyHash> m_CacheKeyIDMap;
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
		[[nodiscard]] TextureLoadRequest LoadTextureAsync(
			const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;

		Texture* GetTexture(TextureID textureId) noexcept;
		const Texture* GetTexture(TextureID textureId) const noexcept;
		const RHITextureDesc* GetTextureDesc(TextureID textureId) const noexcept;
		RHIDescriptorHandle GetSrvDescriptor(TextureID textureId) const noexcept;
		uint32_t GetShaderVisibleSrvIndex(TextureID textureId) const noexcept;

		uint32_t ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;

		TextureID CreateTexture(const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings) noexcept;
		TextureID FindTexture(const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings) const noexcept;

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

		void CreateTextureEntry(
			TextureID id,
			std::string_view textureName,
			const std::filesystem::path& sourcePath = {}) noexcept;
		void CompleteTextureUpload(TextureID textureId, bool succeeded) noexcept;
		bool PublishImportedTexture(
			TextureID textureId,
			TextureSemantic semantic,
			TextureAssetData&& textureData) noexcept;
		void CompleteTextureLoad(
			TextureID textureId,
			TextureSemantic semantic,
			const TaskCompletionInfo& completion,
			TextureAssetData&& textureData) noexcept;
		[[nodiscard]] TaskHandle QueueTextureLoad(
			TextureID textureId,
			const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings,
			TextureSemantic semantic,
			TaskPriority priority) noexcept;
		bool RemoveTexture(TextureID textureId) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;

		TextureIDCounter m_TextureIdCounter{ ReservedTextureCount };
		TextureContainer m_TextureContainer;
		std::unordered_map<TextureID, TaskHandle> m_TextureLoadTasks;
	};
}
