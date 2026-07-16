#pragma once
#include "Core/Hash/KeyHash.h"
#include "Core/Task/TaskTypes.h"
#include "Graphics/VertexData.h"
#include "Graphics/GraphicsTypes.h"
#include "Graphics/GPUStructures.h"
#include "Graphics/ModelImporter.h"
#include "Graphics/SamplerRegistry.h"
#include "Graphics/TextureRegistry.h"

namespace gglab
{
	struct AssetOwnerId
	{
		uint64_t m_Value = 0;

		[[nodiscard]] constexpr bool IsValid() const noexcept { return m_Value != 0; }
		friend constexpr auto operator<=>(const AssetOwnerId&, const AssetOwnerId&) = default;
	};

	struct AssetOwnerIdHash
	{
		size_t operator()(AssetOwnerId owner) const noexcept
		{
			return std::hash<uint64_t>{}(owner.m_Value);
		}
	};

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
	class AssetUploadScheduler;
	class AssetLease;
	class AssetPublicationRetain;
	class AssetOwnerScope;
	class TaskSystem;
	class TransferBatch;
	class TransferManager;
	struct AssetSnapshot;
	struct AssetResourcePublicationStepResult;
	enum class AssetResourcePublicationAbortReason : uint8_t;

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
		class ModelPublicationTransaction;

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
		[[nodiscard]] AssetResourcePublicationStepResult StepModelPublication(
			ModelPublicationTransaction& transaction,
			TaskPriority priority) noexcept;
		void AbortModelPublication(
			ModelPublicationTransaction& transaction,
			AssetResourcePublicationAbortReason reason) noexcept;
		void CompleteModelLoad(
			ModelID modelId,
			uint64_t generation,
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

		struct InterestKey
		{
			AssetInterestKind m_Kind = AssetInterestKind::Model;
			uint64_t m_StableId = 0;

			[[nodiscard]] constexpr auto AsTuple() const noexcept
			{
				return std::tie(m_Kind, m_StableId);
			}
			friend bool operator==(const InterestKey&, const InterestKey&) = default;
		};
		using InterestKeyHash = KeyHash<InterestKey>;

		enum class ModelDependencyOutcome : uint8_t
		{
			Pending,
			Ready,
			Failed,
			Cancelled,
		};

		struct DependencyKey
		{
			AssetInterestKind m_Kind = AssetInterestKind::Mesh;
			uint64_t m_StableId = 0;
			uint64_t m_ContentGeneration = 0;

			[[nodiscard]] constexpr auto AsTuple() const noexcept
			{
				return std::tie(m_Kind, m_StableId, m_ContentGeneration);
			}
			friend bool operator==(const DependencyKey&, const DependencyKey&) = default;
		};
		using DependencyKeyHash = KeyHash<DependencyKey>;

		struct DependentModel
		{
			ModelID m_ModelId{};
			uint64_t m_ContentGeneration = 0;

			friend bool operator==(const DependentModel&, const DependentModel&) = default;
		};

		struct ModelDependencyState
		{
			uint64_t m_ContentGeneration = 0;
			std::unordered_map<DependencyKey, AssetState, DependencyKeyHash> m_DependencyStates;
			uint32_t m_StructuralFailureCount = 0;
			uint32_t m_ReadyCount = 0;
			uint32_t m_PendingCount = 0;
			uint32_t m_FailedCount = 0;
			uint32_t m_CancelledCount = 0;
			uint64_t m_EventUpdateCount = 0;
		};

		struct PendingResidencyEviction
		{
			AssetInterestKind m_Kind = AssetInterestKind::Texture;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
			uint64_t m_ResidentBytes = 0;
			uint64_t m_QuiescedFrame = 0;
		};

		struct LeaseRecord
		{
			InterestKey m_Key{};
			AssetOwnerId m_Owner{};
			uint64_t m_Generation = 0;
			TaskPriority m_Priority = TaskPriority::Normal;
			bool m_IsInternal = false;
		};

		struct InterestRecord
		{
			uint64_t m_Generation = 0;
			TaskPriority m_EffectivePriority = TaskPriority::Normal;
			std::unordered_set<uint64_t> m_LeaseTokens;
		};

		struct PublicationRetainRecord
		{
			uint64_t m_Generation = 0;
			uint32_t m_Count = 0;
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
			const InterestKey& key,
			uint64_t generation) const noexcept;
		void RecomputeInterestPriority(const InterestKey& key) noexcept;
		void ApplyInterestPriority(
			const InterestKey& key,
			uint64_t generation,
			TaskPriority priority) noexcept;
		void CancelAssetIfUnreferenced(
			const InterestKey& key,
			uint64_t generation) noexcept;
		[[nodiscard]] TaskPriority GetEffectivePriority(
			const InterestKey& key,
			TaskPriority fallback = TaskPriority::Normal) const noexcept;
		[[nodiscard]] bool HasActiveInterest(const InterestKey& key) const noexcept;
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
		void OnDependencyStateChanged(
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation,
			AssetState state) noexcept;
		[[nodiscard]] ModelDependencyOutcome EvaluateModelDependenciesByTraversal(
			const Model& model) const noexcept;
		[[nodiscard]] static ModelDependencyOutcome EvaluateModelDependencyCounters(
			const ModelDependencyState& state) noexcept;
		void VerifyModelDependencyState(
			ModelID modelId,
			uint64_t generation,
			ModelDependencyOutcome traversalOutcome) noexcept;
		static void IncrementDependencyCounter(
			ModelDependencyState& state,
			AssetState dependencyState) noexcept;
		static void DecrementDependencyCounter(
			ModelDependencyState& state,
			AssetState dependencyState) noexcept;

	public:
		static void ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept;

	private:
		friend AssetSnapshot BuildAssetSnapshot(const AssetManager& assetManager) noexcept;
		friend class AssetLease;
		friend class AssetPublicationRetain;
		friend class AssetOwnerScope;

		static void SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept;

	private:
		RHIDevice* m_Device = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
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
		std::unordered_map<ModelID, TaskHandle> m_ModelLoadTasks;
		std::unordered_map<ModelID, TaskHandle> m_MeshReloadTasks;
		std::unordered_set<ModelID> m_PendingModels;
		uint64_t m_NextAssetOwnerId = 1;
		uint64_t m_NextAssetLeaseToken = 1;
		std::unordered_map<AssetOwnerId, std::string, AssetOwnerIdHash> m_AssetOwners;
		std::unordered_map<uint64_t, LeaseRecord> m_AssetLeases;
		std::unordered_map<InterestKey, InterestRecord, InterestKeyHash> m_AssetInterests;
		std::unordered_map<InterestKey, PublicationRetainRecord, InterestKeyHash>
			m_PublicationRetains;
		std::unordered_map<ModelID, AssetOwnerId> m_ModelDependencyOwners;
		std::unordered_map<ModelID, std::vector<uint64_t>> m_ModelDependencyLeaseTokens;
		std::unordered_map<ModelID, ModelDependencyState> m_ModelDependencyStates;
		std::unordered_map<DependencyKey, std::vector<DependentModel>, DependencyKeyHash>
			m_ReverseDependencyIndex;
		std::unordered_set<MeshID> m_PublicationOrphanedMeshes;
		uint64_t m_OwnershipPriorityUpdateCount = 0;
		uint64_t m_CpuCancellationCount = 0;
		uint64_t m_ReadyCancellationCount = 0;
		uint64_t m_GpuDeferredCancellationCount = 0;
		uint64_t m_ReadyRetentionCount = 0;
		uint64_t m_PublicationRetainCount = 0;
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
		uint64_t m_DependencyGraphBuildCount = 0;
		uint64_t m_DependencyEventUpdateCount = 0;
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
