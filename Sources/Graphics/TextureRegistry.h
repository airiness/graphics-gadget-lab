#pragma once
#include "Core/Hash/KeyHash.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/TextureAsset.h"
#include "Graphics/TextureLoader.h"

#include <functional>

namespace gglab
{
	class AssetManager;
	class AssetManagerPublicationServices;
	class AssetUploadScheduler;
	class RHIDevice;
	class TaskSystem;
	class TransferBatch;
	class TransferManager;
	struct AssetSnapshot;

	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;

	class TextureRegistry
	{
		struct TextureCacheKey
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
			bool operator==(const TextureCacheKey&) const noexcept = default;
		};
		using TextureCacheKeyHash = KeyHash<TextureCacheKey>;

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
			uint64_t m_Generation = 0;
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
			TextureAssetData&& textureData, TextureSemantic semantic) noexcept;

		[[nodiscard]] bool UploadTexture(
			const TextureUploadData& uploadData,
			TransferBatch& transferBatch) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
		friend class AssetManager;
		friend class AssetManagerPublicationServices;
		friend class EnvironmentLightingSystem;

		void CreateTextureEntry(
			TextureID id,
			std::string_view textureName,
			const std::filesystem::path& sourcePath = {}) noexcept;
		void CompleteTextureUpload(
			TextureID textureId,
			bool succeeded,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool QueueTextureUpload(
			TextureUploadData&& uploadData,
			TaskPriority priority = TaskPriority::Normal,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool PublishImportedTexture(
			TextureID textureId,
			uint64_t generation,
			TextureSemantic semantic,
			TaskPriority priority,
			TextureAssetData&& textureData,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		void CancelTextureIfUnreferenced(TextureID textureId, uint64_t generation) noexcept;
		void UpdateTextureLoadPriority(
			TextureID textureId,
			uint64_t generation,
			TaskPriority priority) noexcept;
		void ReviveTextureInterest(TextureID textureId, uint64_t generation) noexcept;
		[[nodiscard]] TaskHandle RequestTextureResidency(
			TextureID textureId,
			AssetResidencyOperation operation,
			TaskPriority priority) noexcept;
		[[nodiscard]] bool FinalizeTextureEviction(
			TextureID textureId,
			AssetResidencyOperation operation) noexcept;
		void RollbackPublicationTexture(TextureID textureId, uint64_t generation) noexcept;
		void CompleteTextureLoad(
			TextureID textureId,
			uint64_t generation,
			TextureSemantic semantic,
			const TaskCompletionInfo& completion,
			TextureAssetData&& textureData,
			bool residencyReload,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		[[nodiscard]] TaskHandle QueueTextureLoad(
			TextureID textureId,
			const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings,
			TextureSemantic semantic,
			TaskPriority priority,
			bool residencyReload = false,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool RemoveTexture(TextureID textureId) noexcept;
		void SetStateChangeCallback(
			std::function<void(
				TextureID,
				uint64_t,
				AssetContentState,
				AssetResidencyState)> callback) noexcept;
		void SetTextureState(Texture& texture, AssetState state) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;

		TextureIDCounter m_TextureIdCounter{ ReservedTextureCount };
		TextureContainer m_TextureContainer;
		std::unordered_map<TextureID, TaskHandle> m_TextureLoadTasks;
		std::unordered_set<TextureID> m_PublicationOrphanedTextures;
		std::function<void(
			TextureID,
			uint64_t,
			AssetContentState,
			AssetResidencyState)> m_StateChangeCallback;
	};
}
