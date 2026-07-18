#pragma once
#include "Core/Task/TaskTypes.h"
#include "Graphics/Asset/Dependency/AssetDependencyGraph.h"
#include "Graphics/Asset/Dependency/AssetStateEventQueue.h"
#include "Graphics/Asset/Interest/AssetInterestTracker.h"
#include "Graphics/Asset/Loading/AssetLoadCoordinator.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Asset/Residency/AssetResidencyController.h"
#include "Graphics/Asset/Store/MaterialStore.h"
#include "Graphics/Asset/Store/MeshStore.h"
#include "Graphics/Asset/Store/ModelStore.h"
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ModelImporter.h"

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
		uint64_t m_ReadyRetentionCount = 0;
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
	class TextureRegistry;
	class TransferBatch;
	class TransferManager;
	enum class SamplerPreset : uint8_t;
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
			return m_AssetResidencyController.GetConfig();
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

		const Texture* GetTexture(TextureID textureId) const noexcept;

		const Mesh* GetMesh(MeshID meshId) const noexcept;

		const Material* GetMaterial(MaterialID materialId) const noexcept;

		const Model* GetModel(ModelID modelId) const noexcept;

		MeshID AddProceduralMesh(
			std::unique_ptr<Mesh>&& mesh,
			MeshUploadData& meshUploadData) noexcept;
		MaterialID AddProceduralMaterial(std::unique_ptr<Material>&& material) noexcept;
		ModelID AddProceduralModel(std::unique_ptr<Model>&& model) noexcept;

		uint32_t ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept;
		MaterialTextureBindingGPU ResolveTextureBinding(const MaterialTextureBinding& binding,
			ReservedTextureIDIndex fallback,
			SamplerPreset fallbackSampler) const noexcept;

	private:
		[[nodiscard]] Mesh* EditMesh(MeshID meshId) noexcept;
		[[nodiscard]] Model* EditModel(ModelID modelId) noexcept;
		MaterialID AddMaterial(std::unique_ptr<Material>&& material) noexcept;
		[[nodiscard]] bool UploadMesh(
			const MeshUploadData& uploadData,
			TransferBatch& transferBatch,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		bool QueueMeshUpload(
			MeshUploadData&& uploadData,
			TaskPriority priority = TaskPriority::Normal,
			AssetResidencyOperation residencyOperation = {}) noexcept;
		void CompleteMeshUpload(
			MeshID meshId,
			bool succeeded,
			AssetResidencyOperation residencyOperation = {}) noexcept;
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
			AssetResidencyOperation m_Operation{};
			uint64_t m_ResidentBytes = 0;
			uint64_t m_QuiescedFrame = 0;
		};

		AssetOwnerId RegisterAssetOwner(std::string label) noexcept;
		void UnregisterAssetOwner(AssetOwnerId owner) noexcept;
		AssetLease AcquireAssetLease(
			AssetOwnerId owner,
			AssetKind kind,
			uint64_t stableId,
			uint64_t generation,
			TaskPriority priority,
			bool internal = false) noexcept;
		void ReleaseAssetLease(uint64_t leaseToken) noexcept;
		void UpdateAssetLeasePriority(uint64_t leaseToken, TaskPriority priority) noexcept;
		[[nodiscard]] AssetPublicationRetain AcquirePublicationRetain(
			AssetKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept;
		void ReleasePublicationRetain(
			AssetKind kind,
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
			AssetKind kind,
			uint64_t stableId,
			uint64_t generation) const noexcept;
		void FinalizeResidencyEvictions() noexcept;
		void TickResidencyPhase() noexcept;
		[[nodiscard]] AssetResidencyInventorySnapshot
			BuildResidencyInventorySnapshot() const noexcept;
		[[nodiscard]] bool BuildResidencyInventoryEntry(
			AssetKey key,
			AssetResidencyInventoryEntry& entry) const noexcept;
		[[nodiscard]] AssetLifecycle* FindResidencyLifecycle(AssetKey key) noexcept;
		[[nodiscard]] const AssetLifecycle* FindResidencyLifecycle(AssetKey key) const noexcept;
		[[nodiscard]] bool ApplyResidencyAction(
			const AssetResidencyAction& action,
			uint64_t projectedResidentBytes) noexcept;
		void ApplyResidencyPlan(AssetResidencyPlan&& plan) noexcept;
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
		void SetMeshState(
			Mesh& mesh,
			AssetState state,
			AssetResidencyOperation residencyOperation = {},
			AssetStateEventOperationPhase operationPhase =
				AssetStateEventOperationPhase::None) noexcept;
		void RegisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void UnregisterModelDependencies(ModelID modelId, uint64_t generation) noexcept;
		void QueueDependencyStateChange(
			AssetContentVersion contentVersion,
			AssetContentState contentState,
			AssetResidencyState residencyState,
			std::optional<AssetOperationToken> operation = std::nullopt,
			AssetStateEventOperationPhase operationPhase =
				AssetStateEventOperationPhase::None) noexcept;
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

		MeshStore m_MeshStore;
		MaterialStore m_MaterialStore;
		ModelStore m_ModelStore;
		std::unordered_set<ModelID> m_PendingModels;
		AssetLoadCoordinator m_AssetLoadCoordinator;
		AssetInterestTracker m_AssetInterestTracker;
		AssetResidencyController m_AssetResidencyController;
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
		std::vector<PendingResidencyEviction> m_PendingResidencyEvictions;
		uint64_t m_LogicalResidentBytes = 0;
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
			AssetKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept :
			m_Manager(manager),
			m_Kind(kind),
			m_StableId(stableId),
			m_Generation(generation)
		{}

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
