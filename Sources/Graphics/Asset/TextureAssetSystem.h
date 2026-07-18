#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Asset/Residency/AssetResidencyTypes.h"
#include "Graphics/Asset/Store/TextureStore.h"
#include "Graphics/Asset/TextureAssetViews.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/Asset/TextureAsset.h"

#include <optional>

namespace gglab
{
	class AssetManager;
	class AssetLoadCoordinator;
	class AssetManagerPublicationServices;
	class AssetResidencyController;
	class AssetStateEventQueue;
	class AssetUploadScheduler;
	class RHIDevice;
	class TransferBatch;
	class TransferManager;
	struct TextureDecodeFailed;
	struct TextureDecodeSucceeded;

	class TextureAssetSystem
	{
	public:
		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			AssetLoadCoordinator* m_LoadCoordinator = nullptr;
			TransferManager* m_TransferManager = nullptr;
			AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
			AssetStateEventQueue* m_StateEvents = nullptr;
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
			TextureImportSettings m_ImportSettings{};
			TextureAssetData m_TextureData;
			TextureColorSpace m_ColorSpace = TextureColorSpace::Linear;
			AssetContentFingerprint m_ContentFingerprint{};
		};

		struct EvictionFinalizationResult
		{
			bool m_Finalized = false;
			bool m_Cancelled = false;
		};

	public:
		explicit TextureAssetSystem(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(TextureAssetSystem);
		~TextureAssetSystem() = default;

		void InitializeReservedTextures() noexcept;
		void Finalize(const RHIFencePoint& fencePoint) noexcept;

		[[nodiscard]] TextureLoadRequest LoadTextureAsync(
			const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;

	private:
		friend class AssetManager;
		friend class AssetManagerPublicationServices;

		const Texture* GetTexture(TextureID textureId) const noexcept;
		uint32_t ResolveSrvIndex(
			TextureID textureId,
			ReservedTextureIDIndex fallback) const noexcept;
		TextureID CreateTexture(
			const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings) noexcept;
		TextureID FindTexture(
			const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings) const noexcept;
		TextureUploadData MakeTextureUploadData(
			TextureID textureId,
			TextureAssetData&& textureData,
			const TextureImportSettings& importSettings,
			AssetContentFingerprint contentFingerprint = {}) noexcept;
		[[nodiscard]] bool UploadTexture(
			const TextureUploadData& uploadData,
			TransferBatch& transferBatch,
			AssetResidencyOperation residencyOperation = {}) noexcept;

		[[nodiscard]] TextureContentRef GetTextureContentRef(TextureID textureId) const noexcept;
		[[nodiscard]] std::optional<AssetContentFingerprint> GetTextureContentFingerprint(
			TextureContentRef content) const noexcept;
		[[nodiscard]] std::optional<AssetState> GetTextureState(
			TextureContentRef content) const noexcept;
		[[nodiscard]] std::optional<ResidentTextureResource> GetResidentTextureResource(
			TextureContentRef content) const noexcept;
		[[nodiscard]] std::vector<TextureAssetReadInfo> GetTextureAssetReadInfos() const;
		[[nodiscard]] uint32_t GetReloadingTextureCount() const noexcept;
		[[nodiscard]] size_t GetTextureCount() const noexcept { return m_Store.Size(); }
		[[nodiscard]] std::vector<TextureID> GetTextureIds() const;
		[[nodiscard]] bool IsCurrentResidencyOperation(
			const AssetResidencyOperation& operation) const noexcept;
		[[nodiscard]] bool IsCurrentResidencyOperation(
			const AssetOperationToken& operation) const noexcept;
		void CompleteResidencyOperation(const AssetResidencyOperation& operation) noexcept;
		void CompleteResidencyOperation(const AssetOperationToken& operation) noexcept;
		[[nodiscard]] bool RestoreEvictionForShutdown(
			const AssetResidencyOperation& operation) noexcept;
		[[nodiscard]] EvictionFinalizationResult FinalizeEviction(
			const AssetResidencyOperation& operation,
			bool protectedByInterest) noexcept;
		[[nodiscard]] AssetResidencyOperation BeginResidencyOperation(
			const AssetContentVersion& contentVersion,
			AssetResidencyOperationKind kind,
			AssetResidencyController& controller) noexcept;
		[[nodiscard]] TaskHandle RequestResidency(
			TextureID textureId,
			uint64_t generation,
			TaskPriority priority,
			AssetResidencyController& controller) noexcept;
		void MarkUsed(TextureID textureId, uint64_t frame) noexcept;
		[[nodiscard]] bool SetResidencyPolicy(
			TextureID textureId,
			AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] bool BeginPublication(TextureID textureId) noexcept;

		void CreateTextureEntry(
			TextureID id,
			std::string_view textureName,
			TextureImportSettings importSettings,
			const std::filesystem::path& sourcePath = {}) noexcept;
		void CompleteTextureUpload(
			TextureID textureId,
			bool succeeded,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool QueueTextureUpload(
			TextureUploadData&& uploadData,
			TaskPriority priority = TaskPriority::Normal,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool RecordTextureUpload(
			TextureID textureId,
			uint64_t generation,
			const TextureImportSettings& importSettings,
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
			AssetOperationToken operation,
			TextureSemantic semantic,
			const TaskCompletionInfo& completion,
			TextureAssetData&& textureData,
			AssetContentFingerprint contentFingerprint,
			bool residencyReload,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		void RouteTextureDecodeCompletion(TextureDecodeSucceeded&& completion) noexcept;
		void RouteTextureDecodeCompletion(TextureDecodeFailed&& completion) noexcept;
		[[nodiscard]] TaskHandle QueueTextureLoad(
			TextureID textureId,
			const std::filesystem::path& canonicalPath,
			const TextureImportSettings& importSettings,
			TaskPriority priority,
			bool residencyReload = false,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool RemoveTexture(TextureID textureId) noexcept;
		void SetTextureState(
			Texture& texture,
			AssetState state,
			AssetResidencyOperation residencyOperation = {},
			AssetStateEventOperationPhase operationPhase =
				AssetStateEventOperationPhase::None) noexcept;
		[[nodiscard]] Texture* EditTexture(TextureID textureId) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		AssetLoadCoordinator* m_LoadCoordinator = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
		AssetStateEventQueue* m_StateEvents = nullptr;

		TextureIDCounter m_TextureIdCounter{ ReservedTextureCount };
		TextureStore m_Store;
		std::unordered_set<TextureID> m_PublicationOrphanedTextures;
	};
}
