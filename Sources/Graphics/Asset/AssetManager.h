#pragma once
#include "Core/CoreMacros.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"
#include "Graphics/Asset/TextureAssetViews.h"
#include "Graphics/Asset/TextureArtifactCache.h"
#include "Graphics/Asset/ModelImportArtifactCache.h"
#include "Graphics/Asset/Store/MaterialStore.h"
#include "Graphics/Asset/Store/MeshStore.h"
#include "Graphics/Asset/Store/ModelStore.h"
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/Asset/Loading/ModelImporter.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gglab
{
	struct AssetInterestActivity
	{
		AssetKind m_Kind = AssetKind::Model;
		uint64_t m_StableId = 0;
		uint64_t m_Generation = 0;
		uint32_t m_LeaseCount = 0;
		uint32_t m_OwnerCount = 0;
		TaskPriority m_EffectivePriority = TaskPriority::Normal;
	};

	struct AssetOwnershipStatistics
	{
		uint32_t m_OwnerCount = 0;
		uint32_t m_LeaseCount = 0;
		uint32_t m_ManagedAssetCount = 0;
		uint64_t m_PriorityUpdateCount = 0;
		uint64_t m_CpuCancellationCount = 0;
		uint64_t m_ReadyCancellationCount = 0;
		uint64_t m_GpuDeferredCancellationCount = 0;
		uint64_t m_RuntimeRetirementRequestCount = 0;
		uint64_t m_RuntimeRetirementCancellationCount = 0;
		uint64_t m_RuntimeRetirementCount = 0;
		uint32_t m_PendingRuntimeRetirementCount = 0;
		uint64_t m_PublicationRetainCount = 0;
		uint64_t m_PublicationProtectedCancellationCount = 0;
		std::vector<AssetInterestActivity> m_ActiveInterests;
	};

	class RHIDevice;
	class AssetPublicationServicesBase;
	class AssetManagerPublicationServices;
	class AssetUploadScheduler;
	class AssetLease;
	class AssetPublicationRetain;
	class AssetOwnerScope;
	class SamplerRegistry;
	class TaskSystem;
	class TextureAssetSystem;
	class TransferBatch;
	class TransferManager;
	struct AssetSnapshot;

	class AssetManager;
	AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;

	class AssetManager
	{
	public:
		using MaterialTextureSamplingSettings = ModelImportSettings;

		struct ModelLoadRequest
		{
			ModelID m_ModelId{};
			uint64_t m_Generation = 0;
			TaskHandle m_Task{};

			[[nodiscard]] bool IsValid() const noexcept { return m_ModelId.IsValid(); }
		};

		struct TextureLoadRequest
		{
			TextureID m_TextureId{};
			uint64_t m_Generation = 0;
			TaskHandle m_Task{};

			[[nodiscard]] bool IsValid() const noexcept { return m_TextureId.IsValid(); }
		};

		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			TransferManager* m_TransferManager = nullptr;
			AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
			MaterialTextureSamplingSettings m_MaterialTextureSampling{};
			TextureArtifactCacheConfig m_TextureArtifactCache{};
			ModelImportArtifactCacheConfig m_ModelImportArtifactCache{};
			std::filesystem::path m_TextureDerivedDataCacheDirectory;
		};

		struct MeshUploadData
		{
			MeshID m_MeshId{};
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
			ModelMeshUploadSource m_ModelSource;

			[[nodiscard]] std::span<const Vertex> GetVertices() const noexcept
			{
				return m_ModelSource.m_Owner ? m_ModelSource.GetVertices()
					: std::span<const Vertex>(m_VerticesData);
			}

			[[nodiscard]] std::span<const uint32_t> GetIndices() const noexcept
			{
				return m_ModelSource.m_Owner ? m_ModelSource.GetIndices()
					: std::span<const uint32_t>(m_IndicesData);
			}

			void ReleaseBorrowedSource() noexcept { m_ModelSource.Reset(); }
		};

	public:
		explicit AssetManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager();

		[[nodiscard]] ModelLoadRequest LoadModelAsync(const std::filesystem::path& path,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] TextureLoadRequest LoadTextureAsync(const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] AssetOwnerScope CreateOwnerScope() noexcept;
		[[nodiscard]] AssetOwnershipStatistics GetOwnershipStatistics() const;
		void DrainLoadCompletions() noexcept;
		void DrainStateEvents() noexcept;
		// Closes public submission/owner creation before clients release interests.
		void BeginShutdown() noexcept;
		// Terminal owner-thread transition. Requires task/upload producers to be
		// stopped and the RHI context to be idle.
		void PrepareForShutdown(const RHIFencePoint& lastSubmittedFence) noexcept;
		[[nodiscard]] bool IsAcceptingCommands() const noexcept { return m_AcceptingCommands; }
		void SetResidencyConfig(const AssetResidencyConfig& config) noexcept;
		[[nodiscard]] const AssetResidencyConfig& GetResidencyConfig() const noexcept
		{
			return m_AssetResidencyController.GetConfig();
		}
		[[nodiscard]] AssetResidencyStatistics GetResidencyStatistics() const noexcept;
		void Tick() noexcept;
		void MarkModelUsed(ModelID modelId) noexcept;
		void MarkMeshUsed(MeshID meshId) noexcept;
		void MarkTextureUsed(TextureID textureId) noexcept;
		[[nodiscard]] bool SetModelResidencyPolicy(
			ModelID modelId, AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] bool SetMeshResidencyPolicy(
			MeshID meshId, AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] bool SetTextureResidencyPolicy(
			TextureID textureId, AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] TextureContentRef GetTextureContentRef(TextureID textureId) const noexcept;
		[[nodiscard]] std::optional<AssetContentFingerprint> GetTextureContentFingerprint(
			TextureContentRef content) const noexcept;
		[[nodiscard]] std::optional<AssetState> GetTextureState(
			TextureContentRef content) const noexcept;
		[[nodiscard]] std::optional<ResidentTextureResource> GetResidentTextureResource(
			TextureContentRef content) const noexcept;
		[[nodiscard]] TextureArtifactCacheStatistics GetTextureArtifactCacheStatistics()
			const noexcept;
		void ClearTextureArtifactCache() noexcept;
		[[nodiscard]] ModelImportArtifactCacheStatistics GetModelImportArtifactCacheStatistics()
			const noexcept;
		void ClearModelImportArtifactCache() noexcept;
		[[nodiscard]] LocalDerivedDataStoreStatistics GetTextureDerivedDataStatistics()
			const noexcept;
		[[nodiscard]] TextureDerivedDataCoordinatorStatistics
			GetTextureDerivedDataCoordinatorStatistics() const noexcept;
		[[nodiscard]] bool ClearTextureDerivedDataCache() noexcept;

		const Mesh* GetMesh(MeshID meshId) const noexcept;

		const Material* GetMaterial(MaterialID materialId) const noexcept;

		const Model* GetModel(ModelID modelId) const noexcept;

		MeshID AddProceduralMesh(
			std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		MaterialID AddProceduralMaterial(std::unique_ptr<Material>&& material) noexcept;
		ModelID AddProceduralModel(std::unique_ptr<Model>&& model) noexcept;

		uint32_t ResolveSrvIndex(
			TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;

	private:
		[[nodiscard]] Mesh* EditMesh(MeshID meshId) noexcept;
		[[nodiscard]] Model* EditModel(ModelID modelId) noexcept;
		MaterialID AddMaterial(std::unique_ptr<Material>&& material) noexcept;
		[[nodiscard]] bool UploadMesh(MeshUploadData& uploadData, TransferBatch& transferBatch,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool QueueMeshUpload(MeshUploadData&& uploadData,
			TaskPriority priority = TaskPriority::Normal,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		void CompleteMeshUpload(MeshID meshId, bool succeeded,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool RemoveMesh(MeshID meshId) noexcept;
		bool RemoveMaterial(MaterialID materialId) noexcept;
		void RollbackPublicationMesh(MeshID meshId, uint64_t generation) noexcept;
		[[nodiscard]] std::unique_ptr<AssetPublicationServicesBase>
			CreateModelPublicationServices() noexcept;
		void RouteModelImportCompletion(AssetOperationToken operation,
			const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept;
		void RouteMeshReloadCompletion(AssetOperationToken operation,
			const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept;
		void CompleteModelLoad(AssetOperationToken operation, const TaskCompletionInfo& completion,
			ModelImportArtifactHandle artifact) noexcept;
		void CompleteMeshReload(AssetOperationToken operation, const TaskCompletionInfo& completion,
			ModelImportArtifactHandle artifact) noexcept;

		MeshID CreateMesh() noexcept;
		ModelID CreateModel(const std::filesystem::path& canonicalPath,
			AssetState initialState = AssetState::LoadingCpu) noexcept;

		ModelID FindModel(const std::filesystem::path& canonicalPath) const noexcept;
		bool DetachTerminalModelPath(
			const std::filesystem::path& canonicalPath, ModelID modelId) noexcept;
		bool RefreshModelState(ModelID modelId) noexcept;
		void CancelModelIfUnreferenced(ModelID modelId, uint64_t generation) noexcept;
		void CancelMeshIfUnreferenced(MeshID meshId, uint64_t generation) noexcept;
		void RefreshModelDependencyInterests(ModelID modelId, uint64_t generation) noexcept;
		void ReleaseModelDependencyInterests(ModelID modelId) noexcept;
		void UpdateModelDependencyPriorities(ModelID modelId, TaskPriority priority) noexcept;

		struct PendingResidencyEviction
		{
			AssetResidencyOperation m_Operation{};
			uint64_t m_ResidentBytes = 0;
			uint64_t m_QuiescedFrame = 0;
		};

		struct PendingRuntimeRetirement
		{
			AssetContentVersion m_ContentVersion{};
			uint64_t m_QueuedFrame = 0;
		};

		AssetOwnerId RegisterAssetOwner() noexcept;
		void UnregisterAssetOwner(AssetOwnerId owner) noexcept;
		AssetLease AcquireAssetLease(AssetOwnerId owner, AssetKind kind, uint64_t stableId,
			uint64_t generation, TaskPriority priority) noexcept;
		void ReleaseAssetLease(uint64_t leaseToken) noexcept;
		void UpdateAssetLeasePriority(uint64_t leaseToken, TaskPriority priority) noexcept;
		[[nodiscard]] AssetPublicationRetain AcquirePublicationRetain(
			AssetKind kind, uint64_t stableId, uint64_t generation) noexcept;
		void ReleasePublicationRetain(
			AssetKind kind, uint64_t stableId, uint64_t generation) noexcept;
		[[nodiscard]] bool HasPublicationRetain(AssetKey key, uint64_t generation) const noexcept;
		void HandleInterestChange(const AssetInterestChange& change) noexcept;
		void ApplyInterestPriority(
			AssetKey key, uint64_t generation, TaskPriority priority) noexcept;
		void CancelAssetIfUnreferenced(AssetKey key, uint64_t generation) noexcept;
		void QueueRuntimeRetirement(AssetContentVersion contentVersion) noexcept;
		void CancelRuntimeRetirement(AssetContentVersion contentVersion) noexcept;
		void FinalizeRuntimeRetirements() noexcept;
		[[nodiscard]] bool RetireRuntimeEntry(AssetContentVersion contentVersion) noexcept;
		[[nodiscard]] TaskPriority GetEffectivePriority(
			AssetKey key, TaskPriority fallback = TaskPriority::Normal) const noexcept;
		[[nodiscard]] bool HasActiveInterest(AssetKey key) const noexcept;
		[[nodiscard]] bool HasPinnedDependentModel(
			AssetKind kind, uint64_t stableId, uint64_t generation) const noexcept;
		void FinalizeResidencyEvictions() noexcept;
		void TickResidencyPhase() noexcept;
		[[nodiscard]] AssetResidencyInventorySnapshot BuildResidencyInventorySnapshot()
			const noexcept;
		[[nodiscard]] bool BuildResidencyInventoryEntry(
			AssetKey key, AssetResidencyInventoryEntry& entry) const noexcept;
		[[nodiscard]] AssetLifecycle* FindResidencyLifecycle(AssetKey key) noexcept;
		[[nodiscard]] bool ApplyResidencyAction(
			const AssetResidencyAction& action, uint64_t projectedResidentBytes) noexcept;
		void ApplyResidencyPlan(const AssetResidencyPlan& plan) noexcept;
		void RequestModelResidency(ModelID modelId, uint64_t generation) noexcept;
		[[nodiscard]] TaskHandle RequestTextureResidency(
			TextureID textureId, uint64_t generation, TaskPriority priority) noexcept;
		void RequestMeshResidency(
			MeshID meshId, uint64_t generation, TaskPriority priority) noexcept;
		void QueueMeshResidencyReload(ModelID sourceModelId, TaskPriority priority) noexcept;
		void SetMeshState(Mesh& mesh, AssetState state,
			AssetResidencyOperation residencyOperation = {},
			AssetStateEventOperationPhase operationPhase =
			AssetStateEventOperationPhase::None) noexcept;
		void RegisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void UnregisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void QueueDependencyStateChange(AssetContentVersion contentVersion,
			AssetContentState contentState, AssetResidencyState residencyState,
			std::optional<AssetOperationToken> operation = std::nullopt,
			AssetStateEventOperationPhase operationPhase =
			AssetStateEventOperationPhase::None) noexcept;
		[[nodiscard]] ModelDependencyOutcome EvaluateModelDependenciesByTraversal(
			const Model& model) const noexcept;
		void VerifyModelDependencyState(
			ModelID modelId, uint64_t generation, ModelDependencyOutcome traversalOutcome) noexcept;

	public:
		static void ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
		friend class AssetManagerPublicationServices;
		friend class AssetLease;
		friend class AssetPublicationRetain;
		friend class AssetOwnerScope;

		[[nodiscard]] std::vector<TextureAssetReadInfo> GetTextureAssetReadInfos() const;

		static void SetMaterialTexture(Material& material, MaterialTextureSlot slot,
			const MaterialTextureBinding& binding) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
		TextureArtifactCache m_TextureArtifactCache;
		ModelImportArtifactCache m_ModelImportArtifactCache;
		AssetLoadCoordinator m_AssetLoadCoordinator;
		// State events outlive TextureAssetSystem so the injected sink remains valid
		// through texture-domain shutdown and destruction.
		AssetStateEventQueue m_AssetStateEventQueue;
		std::unique_ptr<TextureAssetSystem> m_TextureAssets;
		SamplerRegistry* m_SamplerRegistry = nullptr;
		MaterialTextureSamplingSettings m_MaterialTextureSampling{};

		MeshStore m_MeshStore;
		MaterialStore m_MaterialStore;
		ModelStore m_ModelStore;
		std::unordered_set<ModelID> m_PendingModels;
		AssetInterestTracker m_AssetInterestTracker;
		AssetResidencyController m_AssetResidencyController;
		std::unordered_map<ModelID, AssetOwnerId> m_ModelDependencyOwners;
		std::unordered_map<ModelID, std::vector<uint64_t>> m_ModelDependencyLeaseTokens;
		AssetDependencyGraph m_AssetDependencyGraph;
		std::unordered_set<MeshID> m_PublicationOrphanedMeshes;
		uint64_t m_CpuCancellationCount = 0;
		uint64_t m_ReadyCancellationCount = 0;
		uint64_t m_GpuDeferredCancellationCount = 0;
		uint64_t m_RuntimeRetirementRequestCount = 0;
		uint64_t m_RuntimeRetirementCancellationCount = 0;
		uint64_t m_RuntimeRetirementCount = 0;
		uint64_t m_PublicationProtectedCancellationCount = 0;
		uint64_t m_AssetUsageFrame = 0;
		std::vector<PendingResidencyEviction> m_PendingResidencyEvictions;
		std::vector<PendingRuntimeRetirement> m_PendingRuntimeRetirements;
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_DependencyValidationCount = 0;
		uint64_t m_DependencyValidationMismatchCount = 0;
		bool m_AcceptingCommands = true;
		bool m_IsPreparedForShutdown = false;
	};

	class AssetPublicationRetain
	{
	public:
		AssetPublicationRetain() noexcept = default;
		AssetPublicationRetain(const AssetPublicationRetain&) = delete;
		AssetPublicationRetain& operator=(const AssetPublicationRetain&) = delete;
		AssetPublicationRetain(AssetPublicationRetain&& other) noexcept;
		AssetPublicationRetain& operator=(AssetPublicationRetain&& other) noexcept;
		~AssetPublicationRetain();

		[[nodiscard]] bool IsValid() const noexcept { return m_Manager != nullptr; }
		void Reset() noexcept;

	private:
		friend class AssetManager;
		AssetPublicationRetain(
			AssetManager* manager, AssetKind kind, uint64_t stableId, uint64_t generation) noexcept :
			m_Manager(manager), m_Kind(kind), m_StableId(stableId), m_Generation(generation)
		{
		}

		AssetManager* m_Manager = nullptr;
		AssetKind m_Kind = AssetKind::Model;
		uint64_t m_StableId = 0;
		uint64_t m_Generation = 0;
	};

	class AssetLease
	{
	public:
		AssetLease() noexcept = default;
		AssetLease(const AssetLease&) = delete;
		AssetLease& operator=(const AssetLease&) = delete;
		AssetLease(AssetLease&& other) noexcept;
		AssetLease& operator=(AssetLease&& other) noexcept;
		~AssetLease();

		[[nodiscard]] bool IsValid() const noexcept { return m_Manager && m_LeaseToken != 0; }
		void Reset() noexcept;

	private:
		friend class AssetManager;
		friend class AssetManagerPublicationServices;
		AssetLease(AssetManager* manager, uint64_t leaseToken) noexcept :
			m_Manager(manager), m_LeaseToken(leaseToken)
		{
		}

		AssetManager* m_Manager = nullptr;
		uint64_t m_LeaseToken = 0;
	};

	class AssetOwnerScope
	{
	public:
		AssetOwnerScope() noexcept = default;
		AssetOwnerScope(const AssetOwnerScope&) = delete;
		AssetOwnerScope& operator=(const AssetOwnerScope&) = delete;
		AssetOwnerScope(AssetOwnerScope&& other) noexcept;
		AssetOwnerScope& operator=(AssetOwnerScope&& other) noexcept;
		~AssetOwnerScope();

		[[nodiscard]] AssetManager::ModelLoadRequest LoadModelAsync(
			const std::filesystem::path& path,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] AssetManager::TextureLoadRequest LoadTextureAsync(
			const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] bool RetainTexture(
			TextureContentRef content, TaskPriority priority = TaskPriority::High) noexcept;
		void Reset() noexcept;

	private:
		friend class AssetManager;
		AssetOwnerScope(AssetManager* manager, AssetOwnerId owner) noexcept :
			m_Manager(manager), m_Owner(owner)
		{
		}

		AssetManager* m_Manager = nullptr;
		AssetOwnerId m_Owner{};
		std::vector<AssetLease> m_Leases;
	};
}
