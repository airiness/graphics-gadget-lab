#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ModelImporter.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureRegistry.h"

namespace gglab
{
	enum class AssetInterestKind : uint8_t
	{
		Model,
		Texture,
		Mesh,
	};

	struct AssetInterestActivity
	{
		AssetInterestKind m_Kind = AssetInterestKind::Model;
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
		uint64_t m_ReadyRetentionCount = 0;
		uint64_t m_PublicationRetainCount = 0;
		uint64_t m_PublicationProtectedCancellationCount = 0;
		std::vector<AssetInterestActivity> m_ActiveInterests;
	};

	struct AssetResidencyConfig
	{
		bool m_EnableAutomaticEviction = false;
		uint64_t m_HighWatermarkBytes = 512ull * 1024ull * 1024ull;
		uint64_t m_LowWatermarkBytes = 384ull * 1024ull * 1024ull;
		uint64_t m_MinUnusedFrames = 120;
		uint32_t m_MaxEvictionsPerFrame = 8;
	};

	struct AssetResidencyStatistics
	{
		AssetResidencyConfig m_Config{};
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_PendingEvictionBytes = 0;
		uint32_t m_PendingEvictionCount = 0;
		uint32_t m_ReloadingAssetCount = 0;
		uint64_t m_EvictionCount = 0;
		uint64_t m_EvictedBytes = 0;
		uint64_t m_EvictionCancellationCount = 0;
		uint64_t m_ReloadRequestCount = 0;
		uint64_t m_ReloadCoalescedCount = 0;
		uint32_t m_LastFrameReloadRequestCount = 0;
		uint32_t m_ReloadRequestHighWatermark = 0;
	};

	class RHIDevice;
	class AssetPublicationServicesBase;
	class AssetManagerPublicationServices;
	class AssetUploadScheduler;
	class AssetLease;
	class AssetPublicationRetain;
	class AssetOwnerScope;
	class TaskSystem;
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

		using TextureLoadRequest = TextureRegistry::TextureLoadRequest;

		struct CreateInfo
		{
			RHIDevice* m_Device = nullptr;
			TaskSystem* m_TaskSystem = nullptr;
			TransferManager* m_TransferManager = nullptr;
			AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
			TextureRegistry* m_TextureRegistry = nullptr;
			SamplerRegistry* m_SamplerRegistry = nullptr;
			MaterialTextureSamplingSettings m_MaterialTextureSampling{};
		};

		struct MeshUploadData
		{
			MeshID m_MeshId{};
			std::vector<Vertex> m_VerticesData;
			std::vector<uint32_t> m_IndicesData;
		};

	private:
		struct MeshContainer
		{
			std::unordered_map<MeshID, std::unique_ptr<Mesh>> m_MeshIDMap;
		};

		struct MaterialContainer
		{
			std::unordered_map<MaterialID, std::unique_ptr<Material>> m_MaterialIDMap;
		};

		struct ModelContainer
		{
			std::unordered_map<std::filesystem::path, ModelID> m_PathIDMap;
			std::unordered_map<ModelID, std::unique_ptr<Model>> m_ModelIDMap;
		};

	public:
		explicit AssetManager(const CreateInfo& createInfo) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(AssetManager);
		~AssetManager();

		[[nodiscard]] ModelLoadRequest LoadModelAsync(
			const std::filesystem::path& path,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] TextureLoadRequest LoadTextureAsync(
			const std::filesystem::path& path,
			TextureSemantic semantic = TextureSemantic::GenericColor,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		[[nodiscard]] AssetOwnerScope CreateOwnerScope(std::string label) noexcept;
		[[nodiscard]] AssetOwnershipStatistics GetOwnershipStatistics() const;
		void DrainLoadCompletions() noexcept;
		void DrainStateEvents() noexcept;
		void SetResidencyConfig(const AssetResidencyConfig& config) noexcept;
		[[nodiscard]] const AssetResidencyConfig& GetResidencyConfig() const noexcept
		{
			return m_ResidencyConfig;
		}
		[[nodiscard]] AssetResidencyStatistics GetResidencyStatistics() const noexcept;
		void Tick() noexcept;
		void MarkModelUsed(ModelID modelId) noexcept;
		void MarkMeshUsed(MeshID meshId) noexcept;
		void MarkTextureUsed(TextureID textureId) noexcept;
		[[nodiscard]] bool SetModelResidencyPolicy(
			ModelID modelId,
			AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] bool SetMeshResidencyPolicy(
			MeshID meshId,
			AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] bool SetTextureResidencyPolicy(
			TextureID textureId,
			AssetResidencyPolicy policy) noexcept;
		[[nodiscard]] uint64_t GetAssetUsageFrame() const noexcept
		{
			return m_AssetUsageFrame;
		}

		Texture* GetTexture(TextureID textureId) noexcept;
		const Texture* GetTexture(TextureID textureId) const noexcept;

		Mesh* GetMesh(MeshID meshId) noexcept;
		const Mesh* GetMesh(MeshID meshId) const noexcept;

		Material* GetMaterial(MaterialID materialId) noexcept;
		const Material* GetMaterial(MaterialID materialId) const noexcept;

		Model* GetModel(ModelID modelId) noexcept;
		const Model* GetModel(ModelID modelId) const noexcept;

		MeshID AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept;
		MaterialID AddMaterial(std::unique_ptr<Material>&& material) noexcept;
		ModelID AddModel(std::unique_ptr<Model>&& model) noexcept;

		uint32_t ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;
		MaterialTextureBindingGPU ResolveTextureBinding(const MaterialTextureBinding& binding,
			ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler) const noexcept;

	private:
		[[nodiscard]] bool UploadMesh(
			const MeshUploadData& uploadData,
			TransferBatch& transferBatch) noexcept;
		bool QueueMeshUpload(
			MeshUploadData&& uploadData,
			TaskPriority priority = TaskPriority::Normal) noexcept;
		void CompleteMeshUpload(MeshID meshId, bool succeeded) noexcept;
		bool RemoveMesh(MeshID meshId) noexcept;
		bool RemoveMaterial(MaterialID materialId) noexcept;
		void RollbackPublicationMesh(MeshID meshId, uint64_t generation) noexcept;
		[[nodiscard]] std::unique_ptr<AssetPublicationServicesBase>
			CreateModelPublicationServices() noexcept;
		void RouteModelImportCompletion(
			AssetOperationToken operation,
			const TaskCompletionInfo& completion,
			ImportedModel&& importedModel) noexcept;
		void RouteMeshReloadCompletion(
			AssetOperationToken operation,
			const TaskCompletionInfo& completion,
			ImportedModel&& importedModel) noexcept;
		void CompleteModelLoad(
			AssetOperationToken operation,
			const TaskCompletionInfo& completion,
			ImportedModel&& importedModel) noexcept;
		void CompleteMeshReload(
			AssetOperationToken operation,
			const TaskCompletionInfo& completion,
			ImportedModel&& importedModel) noexcept;

		MeshID CreateMesh() noexcept;
		MaterialID CreateMaterial() noexcept;
		ModelID CreateModel(
			const std::filesystem::path& canonicalPath,
			AssetState initialState = AssetState::LoadingCpu) noexcept;

		ModelID FindModel(const std::filesystem::path& canonicalPath) const noexcept;
		bool DetachTerminalModelPath(
			const std::filesystem::path& canonicalPath,
			ModelID modelId) noexcept;
		bool RefreshModelState(ModelID modelId) noexcept;
		void CancelModelIfUnreferenced(ModelID modelId, uint64_t generation) noexcept;
		void CancelMeshIfUnreferenced(MeshID meshId, uint64_t generation) noexcept;
		void RefreshModelDependencyInterests(ModelID modelId, uint64_t generation) noexcept;
		void ReleaseModelDependencyInterests(ModelID modelId) noexcept;
		void UpdateModelDependencyPriorities(ModelID modelId, TaskPriority priority) noexcept;

		struct PendingResidencyEviction
		{
			AssetInterestKind m_Kind = AssetInterestKind::Texture;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
			uint64_t m_ResidentBytes = 0;
			uint64_t m_QuiescedFrame = 0;
		};

		AssetOwnerId RegisterAssetOwner(std::string label) noexcept;
		void UnregisterAssetOwner(AssetOwnerId owner) noexcept;
		AssetLease AcquireAssetLease(
			AssetOwnerId owner,
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation,
			TaskPriority priority,
			bool internal = false) noexcept;
		void ReleaseAssetLease(uint64_t leaseToken) noexcept;
		void UpdateAssetLeasePriority(uint64_t leaseToken, TaskPriority priority) noexcept;
		[[nodiscard]] AssetPublicationRetain AcquirePublicationRetain(
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept;
		void ReleasePublicationRetain(
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept;
		[[nodiscard]] bool HasPublicationRetain(
			AssetKey key,
			uint64_t generation) const noexcept;
		void HandleInterestChange(const AssetInterestChange& change) noexcept;
		void ApplyInterestPriority(
			AssetKey key,
			uint64_t generation,
			TaskPriority priority) noexcept;
		void CancelAssetIfUnreferenced(
			AssetKey key,
			uint64_t generation) noexcept;
		[[nodiscard]] TaskPriority GetEffectivePriority(
			AssetKey key,
			TaskPriority fallback = TaskPriority::Normal) const noexcept;
		[[nodiscard]] bool HasActiveInterest(AssetKey key) const noexcept;
		[[nodiscard]] bool HasPinnedDependentModel(
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation) const noexcept;
		void MarkAssetUsed(AssetLifecycle& lifecycle) noexcept;
		void FinalizeResidencyEvictions() noexcept;
		void SelectResidencyEvictions() noexcept;
		void RequestModelResidency(ModelID modelId, uint64_t generation) noexcept;
		[[nodiscard]] TaskHandle RequestTextureResidency(
			TextureID textureId,
			uint64_t generation,
			TaskPriority priority) noexcept;
		void RequestMeshResidency(
			MeshID meshId,
			uint64_t generation,
			TaskPriority priority) noexcept;
		void QueueMeshResidencyReload(
			ModelID sourceModelId,
			TaskPriority priority) noexcept;
		[[nodiscard]] uint64_t ComputeLogicalResidentBytes() const noexcept;
		[[nodiscard]] static bool SetResidencyPolicy(
			AssetLifecycle& lifecycle,
			AssetResidencyPolicy policy,
			bool isReserved) noexcept;
		void SetMeshState(Mesh& mesh, AssetState state) noexcept;
		void RegisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void UnregisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void QueueDependencyStateChange(
			AssetContentVersion contentVersion,
			AssetContentState contentState,
			AssetResidencyState residencyState) noexcept;
		[[nodiscard]] ModelDependencyOutcome EvaluateModelDependenciesByTraversal(
			const Model& model) const noexcept;
		void VerifyModelDependencyState(
			ModelID modelId,
			uint64_t generation,
			ModelDependencyOutcome traversalOutcome) noexcept;

	public:
		static void ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
		friend class AssetManagerPublicationServices;
		friend class AssetLease;
		friend class AssetPublicationRetain;
		friend class AssetOwnerScope;

		static void SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TransferManager* m_TransferManager = nullptr;
		AssetUploadScheduler* m_AssetUploadScheduler = nullptr;
		TextureRegistry* m_TextureRegistry = nullptr;
		SamplerRegistry* m_SamplerRegistry = nullptr;
		MaterialTextureSamplingSettings m_MaterialTextureSampling{};

		MeshIDCounter m_MeshIdCounter{ ReservedMeshCount };
		MaterialIDCounter m_MaterialIdCounter{ ReservedMaterialCount };
		ModelIDCounter m_ModelIdCounter{ ReservedModelCount };

		MeshContainer m_MeshContainer;
		MaterialContainer m_MaterialContainer;
		ModelContainer m_ModelContainer;
		std::unordered_set<ModelID> m_PendingModels;
		AssetLoadCoordinator m_AssetLoadCoordinator;
		AssetInterestTracker m_AssetInterestTracker;
		std::unordered_map<ModelID, AssetOwnerId> m_ModelDependencyOwners;
		std::unordered_map<ModelID, std::vector<uint64_t>> m_ModelDependencyLeaseTokens;
		AssetDependencyGraph m_AssetDependencyGraph;
		AssetStateEventQueue m_AssetStateEventQueue;
		std::unordered_set<MeshID> m_PublicationOrphanedMeshes;
		uint64_t m_CpuCancellationCount = 0;
		uint64_t m_ReadyCancellationCount = 0;
		uint64_t m_GpuDeferredCancellationCount = 0;
		uint64_t m_ReadyRetentionCount = 0;
		uint64_t m_PublicationProtectedCancellationCount = 0;
		uint64_t m_AssetUsageFrame = 0;
		AssetResidencyConfig m_ResidencyConfig{};
		std::vector<PendingResidencyEviction> m_PendingResidencyEvictions;
		uint64_t m_LogicalResidentBytes = 0;
		uint64_t m_ResidencyEvictionCount = 0;
		uint64_t m_ResidencyEvictedBytes = 0;
		uint64_t m_ResidencyEvictionCancellationCount = 0;
		uint64_t m_ResidencyReloadRequestCount = 0;
		uint64_t m_ResidencyReloadCoalescedCount = 0;
		uint32_t m_CurrentFrameReloadRequestCount = 0;
		uint32_t m_LastFrameReloadRequestCount = 0;
		uint32_t m_ReloadRequestHighWatermark = 0;
		uint64_t m_DependencyValidationCount = 0;
		uint64_t m_DependencyValidationMismatchCount = 0;
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
			AssetManager* manager,
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept :
			m_Manager(manager),
			m_Kind(kind),
			m_StableId(stableId),
			m_Generation(generation)
		{}

		AssetManager* m_Manager = nullptr;
		AssetInterestKind m_Kind = AssetInterestKind::Model;
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
			m_Manager(manager),
			m_LeaseToken(leaseToken)
		{}

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
		void Reset() noexcept;
		[[nodiscard]] AssetOwnerId GetOwnerId() const noexcept { return m_Owner; }

	private:
		friend class AssetManager;
		AssetOwnerScope(AssetManager* manager, AssetOwnerId owner) noexcept :
			m_Manager(manager),
			m_Owner(owner)
		{}

		AssetManager* m_Manager = nullptr;
		AssetOwnerId m_Owner{};
		std::vector<AssetLease> m_Leases;
	};
}
