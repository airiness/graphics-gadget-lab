#include "Core/Precompiled.h"
#include "Graphics/AssetManager.h"
#include "Core/Task/TaskSystem.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/Publication/ModelPublicationJob.h"
#include "Graphics/AssetUploadScheduler.h"
#include "Graphics/TransferManager.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"
#include <algorithm>
#include <limits>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsTerminalAssetState(AssetState state) noexcept
		{
			return state == AssetState::Failed || state == AssetState::Cancelled;
		}

		[[nodiscard]] AssetKind ToAssetKind(AssetInterestKind kind) noexcept
		{
			switch (kind)
			{
			case AssetInterestKind::Model: return AssetKind::Model;
			case AssetInterestKind::Texture: return AssetKind::Texture;
			case AssetInterestKind::Mesh: return AssetKind::Mesh;
			}
			return AssetKind::Unknown;
		}

		[[nodiscard]] AssetInterestKind ToAssetInterestKind(AssetKind kind) noexcept
		{
			switch (kind)
			{
			case AssetKind::Texture: return AssetInterestKind::Texture;
			case AssetKind::Mesh: return AssetInterestKind::Mesh;
			case AssetKind::Unknown:
			case AssetKind::Model:
			case AssetKind::Material:
				return AssetInterestKind::Model;
			}
			return AssetInterestKind::Model;
		}

		[[nodiscard]] std::array<TextureID, 5> GetMaterialTextureIds(
			const MaterialProperties& material) noexcept
		{
			return {
				material.m_BaseColorBinding.m_TextureId,
				material.m_EmissiveBinding.m_TextureId,
				material.m_MetallicRoughnessBinding.m_TextureId,
				material.m_NormalBinding.m_TextureId,
				material.m_OcclusionBinding.m_TextureId,
			};
		}

		[[nodiscard]] AssetStreamingWorkEstimate EstimateMeshUpload(
			const AssetManager::MeshUploadData& uploadData) noexcept
		{
			const uint64_t vertexBytes = static_cast<uint64_t>(
				uploadData.m_VerticesData.size()) * sizeof(Vertex);
			const uint64_t indexBytes = static_cast<uint64_t>(
				uploadData.m_IndicesData.size()) * sizeof(uint32_t);
			return {
				.m_SourceBytes = vertexBytes + indexBytes,
				.m_StagingBytes = vertexBytes + indexBytes,
				.m_OperationCount = 2,
			};
		}

		[[nodiscard]] AssetStreamingWorkEstimate EstimateImportedModel(
			const ImportedModel& model) noexcept
		{
			AssetStreamingWorkEstimate estimate{};
			for (const ImportedTexture& texture : model.m_Textures)
			{
				estimate.m_SourceBytes += texture.m_Data.m_Pixels.size();
			}
			for (const ImportedMesh& mesh : model.m_Meshes)
			{
				estimate.m_SourceBytes +=
					static_cast<uint64_t>(mesh.m_Vertices.size()) * sizeof(Vertex);
				estimate.m_SourceBytes +=
					static_cast<uint64_t>(mesh.m_Indices.size()) * sizeof(uint32_t);
			}
			return estimate;
		}

		[[nodiscard]] uint64_t EstimateTextureResidentBytes(
			const Texture& texture) noexcept
		{
			if (!texture.m_Texture.IsValid())
			{
				return 0;
			}
			const uint64_t bytesPerTexel =
				GetRHIFormatInfo(texture.m_Desc.m_Format).m_BytesPerBlock;
			uint64_t bytes = 0;
			for (uint32_t mip = 0; mip < texture.m_Desc.m_MipLevels; ++mip)
			{
				const uint64_t width = std::max<uint32_t>(1, texture.m_Desc.m_Extent.m_Width >> mip);
				const uint64_t height = std::max<uint32_t>(1, texture.m_Desc.m_Extent.m_Height >> mip);
				const uint64_t depth = std::max<uint32_t>(1, texture.m_Desc.m_Extent.m_Depth >> mip);
				bytes += width * height * depth * bytesPerTexel;
			}
			return bytes * texture.m_Desc.m_ArraySize * texture.m_Desc.m_SampleCount;
		}

		[[nodiscard]] uint64_t EstimateMeshResidentBytes(const Mesh& mesh) noexcept
		{
			return static_cast<uint64_t>(mesh.m_VertexBufferBinding.m_SizeInBytes) +
				mesh.m_IndexBufferBinding.m_SizeInBytes;
		}
	}

	AssetManager::AssetManager(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device),
		m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler),
		m_TextureRegistry(createInfo.m_TextureRegistry),
		m_SamplerRegistry(createInfo.m_SamplerRegistry),
		m_MaterialTextureSampling(createInfo.m_MaterialTextureSampling),
		m_AssetLoadCoordinator({ .m_TaskSystem = createInfo.m_TaskSystem })
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
		GGLAB_ASSERT_MSG(m_TextureRegistry != nullptr, "TextureRegistry is null!");
		GGLAB_ASSERT_MSG(m_SamplerRegistry != nullptr, "SamplerRegistry is null!");
		m_TextureRegistry->SetStateChangeCallback(
			[this](
				TextureID textureId,
				uint64_t generation,
				AssetContentState contentState,
				AssetResidencyState residencyState) noexcept
			{
				QueueDependencyStateChange(
					MakeAssetContentVersion(textureId, generation),
					contentState,
					residencyState);
			});
	}

	AssetManager::~AssetManager()
	{
		m_TextureRegistry->SetStateChangeCallback({});
		GGLAB_ASSERT_MSG(
			!m_AssetInterestTracker.HasLeases() &&
				!m_AssetInterestTracker.HasInterests(),
			"AssetManager destroyed while asset leases are still active.");
		GGLAB_ASSERT_MSG(
			m_ModelDependencyOwners.empty() && m_ModelDependencyLeaseTokens.empty(),
			"AssetManager destroyed while model dependency ownership is still active.");
		GGLAB_ASSERT_MSG(
			!m_AssetInterestTracker.HasOwners(),
			"AssetManager destroyed while asset owner scopes are still registered.");
		GGLAB_ASSERT_MSG(
			!m_AssetInterestTracker.HasPublicationRetains(),
			"AssetManager destroyed while publication retains are still active.");
		GGLAB_ASSERT_MSG(
			m_PublicationOrphanedMeshes.empty(),
			"AssetManager destroyed while publication mesh rollback is pending GPU completion.");
		GGLAB_ASSERT_MSG(
			!m_AssetLoadCoordinator.HasActiveOperations() &&
				!m_AssetLoadCoordinator.HasPendingCompletions(),
			"AssetManager destroyed before asset load operations were drained.");
		GGLAB_ASSERT_MSG(
			!m_AssetStateEventQueue.HasPendingEvents(),
			"AssetManager destroyed before asset state events were drained.");
	}

	AssetPublicationRetain::AssetPublicationRetain(
		AssetPublicationRetain&& other) noexcept :
		m_Manager(std::exchange(other.m_Manager, nullptr)),
		m_Kind(std::exchange(other.m_Kind, AssetInterestKind::Model)),
		m_StableId(std::exchange(other.m_StableId, 0)),
		m_Generation(std::exchange(other.m_Generation, 0))
	{}

	AssetPublicationRetain& AssetPublicationRetain::operator=(
		AssetPublicationRetain&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Manager = std::exchange(other.m_Manager, nullptr);
			m_Kind = std::exchange(other.m_Kind, AssetInterestKind::Model);
			m_StableId = std::exchange(other.m_StableId, 0);
			m_Generation = std::exchange(other.m_Generation, 0);
		}
		return *this;
	}

	AssetPublicationRetain::~AssetPublicationRetain()
	{
		Reset();
	}

	void AssetPublicationRetain::Reset() noexcept
	{
		if (m_Manager)
		{
			m_Manager->ReleasePublicationRetain(
				m_Kind,
				m_StableId,
				m_Generation);
		}
		m_Manager = nullptr;
		m_Kind = AssetInterestKind::Model;
		m_StableId = 0;
		m_Generation = 0;
	}

	AssetLease::AssetLease(AssetLease&& other) noexcept :
		m_Manager(std::exchange(other.m_Manager, nullptr)),
		m_LeaseToken(std::exchange(other.m_LeaseToken, 0))
	{}

	AssetLease& AssetLease::operator=(AssetLease&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Manager = std::exchange(other.m_Manager, nullptr);
			m_LeaseToken = std::exchange(other.m_LeaseToken, 0);
		}
		return *this;
	}

	AssetLease::~AssetLease()
	{
		Reset();
	}

	void AssetLease::Reset() noexcept
	{
		if (m_Manager && m_LeaseToken != 0)
		{
			m_Manager->ReleaseAssetLease(m_LeaseToken);
		}
		m_Manager = nullptr;
		m_LeaseToken = 0;
	}

	AssetOwnerScope::AssetOwnerScope(AssetOwnerScope&& other) noexcept :
		m_Manager(std::exchange(other.m_Manager, nullptr)),
		m_Owner(std::exchange(other.m_Owner, {})),
		m_Leases(std::move(other.m_Leases))
	{}

	AssetOwnerScope& AssetOwnerScope::operator=(AssetOwnerScope&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			if (m_Manager && m_Owner.IsValid())
			{
				m_Manager->UnregisterAssetOwner(m_Owner);
			}
			m_Manager = std::exchange(other.m_Manager, nullptr);
			m_Owner = std::exchange(other.m_Owner, {});
			m_Leases = std::move(other.m_Leases);
		}
		return *this;
	}

	AssetOwnerScope::~AssetOwnerScope()
	{
		Reset();
		if (m_Manager && m_Owner.IsValid())
		{
			m_Manager->UnregisterAssetOwner(m_Owner);
		}
	}

	AssetManager::ModelLoadRequest AssetOwnerScope::LoadModelAsync(
		const std::filesystem::path& path,
		TaskPriority priority) noexcept
	{
		if (!m_Manager || !m_Owner.IsValid())
		{
			return {};
		}
		AssetManager::ModelLoadRequest request = m_Manager->LoadModelAsync(path, priority);
		if (request.IsValid())
		{
			m_Leases.emplace_back(m_Manager->AcquireAssetLease(
				m_Owner,
				AssetInterestKind::Model,
				request.m_ModelId.Value(),
				request.m_Generation,
				priority));
		}
		return request;
	}

	AssetManager::TextureLoadRequest AssetOwnerScope::LoadTextureAsync(
		const std::filesystem::path& path,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		if (!m_Manager || !m_Owner.IsValid())
		{
			return {};
		}
		AssetManager::TextureLoadRequest request =
			m_Manager->LoadTextureAsync(path, semantic, priority);
		if (request.IsValid())
		{
			m_Leases.emplace_back(m_Manager->AcquireAssetLease(
				m_Owner,
				AssetInterestKind::Texture,
				request.m_TextureId.Value(),
				request.m_Generation,
				priority));
		}
		return request;
	}

	void AssetOwnerScope::Reset() noexcept
	{
		m_Leases.clear();
	}

	AssetOwnerScope AssetManager::CreateOwnerScope(std::string label) noexcept
	{
		return AssetOwnerScope(this, RegisterAssetOwner(std::move(label)));
	}

	AssetOwnershipStatistics AssetManager::GetOwnershipStatistics() const
	{
		const AssetInterestTrackerStatistics trackerStatistics =
			m_AssetInterestTracker.GetStatistics();
		AssetOwnershipStatistics statistics{};
		statistics.m_OwnerCount = trackerStatistics.m_OwnerCount;
		statistics.m_LeaseCount = trackerStatistics.m_LeaseCount;
		statistics.m_ManagedAssetCount = trackerStatistics.m_ManagedAssetCount;
		statistics.m_PriorityUpdateCount = trackerStatistics.m_PriorityUpdateCount;
		statistics.m_CpuCancellationCount = m_CpuCancellationCount;
		statistics.m_ReadyCancellationCount = m_ReadyCancellationCount;
		statistics.m_GpuDeferredCancellationCount = m_GpuDeferredCancellationCount;
		statistics.m_ReadyRetentionCount = m_ReadyRetentionCount;
		statistics.m_PublicationRetainCount = trackerStatistics.m_PublicationRetainCount;
		statistics.m_PublicationProtectedCancellationCount =
			m_PublicationProtectedCancellationCount;
		statistics.m_ActiveInterests.reserve(trackerStatistics.m_ActiveInterests.size());
		for (const TrackedAssetInterestActivity& interest :
			trackerStatistics.m_ActiveInterests)
		{
			statistics.m_ActiveInterests.push_back({
				.m_Kind = ToAssetInterestKind(interest.m_ContentVersion.m_Key.m_Kind),
				.m_StableId = interest.m_ContentVersion.m_Key.m_StableId,
				.m_Generation = interest.m_ContentVersion.m_ContentGeneration,
				.m_LeaseCount = interest.m_LeaseCount,
				.m_OwnerCount = interest.m_OwnerCount,
				.m_EffectivePriority = interest.m_EffectivePriority,
			});
		}
		std::ranges::sort(statistics.m_ActiveInterests,
			[](const AssetInterestActivity& lhs, const AssetInterestActivity& rhs) noexcept
			{
				if (lhs.m_Kind != rhs.m_Kind)
				{
					return lhs.m_Kind < rhs.m_Kind;
				}
				return lhs.m_StableId < rhs.m_StableId;
			});
		return statistics;
	}

	void AssetManager::DrainLoadCompletions() noexcept
	{
		std::vector<AssetLoadCompletion> completions;
		m_AssetLoadCoordinator.DrainCompletions(completions);
		for (AssetLoadCompletion& completion : completions)
		{
			std::visit(
				[this](auto& result) noexcept
				{
					using Result = std::remove_cvref_t<decltype(result)>;
					if constexpr (std::same_as<Result, ModelImportSucceeded>)
					{
						RouteModelImportCompletion(
							result.m_Operation,
							result.m_Completion,
							std::move(result.m_Model));
					}
					else if constexpr (std::same_as<Result, ModelImportFailed>)
					{
						RouteModelImportCompletion(
							result.m_Operation,
							result.m_Completion,
							{});
					}
					else if constexpr (std::same_as<Result, MeshReloadSucceeded>)
					{
						RouteMeshReloadCompletion(
							result.m_Operation,
							result.m_Completion,
							std::move(result.m_Model));
					}
					else
					{
						RouteMeshReloadCompletion(
							result.m_Operation,
							result.m_Completion,
							{});
					}
				},
				completion);
		}
	}

	void AssetManager::DrainStateEvents() noexcept
	{
		std::vector<AssetStateEvent> events;
		m_AssetStateEventQueue.Drain(events);
		std::vector<AssetDependencyChange> changes;
		for (const AssetStateEvent& event : events)
		{
			GGLAB_UNUSED(m_AssetDependencyGraph.ApplyStatus(event.m_Status, changes));
			for (const AssetDependencyChange& change : changes)
			{
				if (change.m_Model.m_Key.m_Kind != AssetKind::Model)
				{
					continue;
				}
				const ModelID modelId{ static_cast<uint32_t>(
					change.m_Model.m_Key.m_StableId) };
				Model* model = EditModel(modelId);
				if (!model ||
					model->m_ContentGeneration !=
						change.m_Model.m_ContentGeneration)
				{
					continue;
				}
				if (model->m_State == AssetState::Ready &&
					change.m_CurrentOutcome != ModelDependencyOutcome::Ready)
				{
					SetAssetState(*model, AssetState::GpuProcessing);
				}
				m_PendingModels.insert(modelId);
			}
		}
	}

	void AssetManager::SetResidencyConfig(const AssetResidencyConfig& config) noexcept
	{
		m_ResidencyConfig = config;
		m_ResidencyConfig.m_LowWatermarkBytes = std::min(
			m_ResidencyConfig.m_LowWatermarkBytes,
			m_ResidencyConfig.m_HighWatermarkBytes);
	}

	AssetResidencyStatistics AssetManager::GetResidencyStatistics() const noexcept
	{
		AssetResidencyStatistics statistics{
			.m_Config = m_ResidencyConfig,
			.m_LogicalResidentBytes = m_LogicalResidentBytes,
			.m_PendingEvictionCount = static_cast<uint32_t>(
				m_PendingResidencyEvictions.size()),
			.m_EvictionCount = m_ResidencyEvictionCount,
			.m_EvictedBytes = m_ResidencyEvictedBytes,
			.m_EvictionCancellationCount = m_ResidencyEvictionCancellationCount,
			.m_ReloadRequestCount = m_ResidencyReloadRequestCount,
			.m_ReloadCoalescedCount = m_ResidencyReloadCoalescedCount,
			.m_LastFrameReloadRequestCount = m_LastFrameReloadRequestCount,
			.m_ReloadRequestHighWatermark = m_ReloadRequestHighWatermark,
			.m_PlanningCount = m_ResidencyPlanningCount,
			.m_LastPlanFrame = m_LastResidencyPlanFrame,
			.m_LastPlannedActionCount = m_LastPlannedResidencyActionCount,
			.m_LastPlannedBytes = m_LastPlannedResidencyBytes,
		};
		for (const PendingResidencyEviction& eviction : m_PendingResidencyEvictions)
		{
			statistics.m_PendingEvictionBytes += eviction.m_ResidentBytes;
		}
		for (const auto& mesh : m_MeshStore.Entries() | std::views::values)
		{
			statistics.m_ReloadingAssetCount += mesh->m_IsReloading ? 1u : 0u;
		}
		for (const auto& texture :
			m_TextureRegistry->m_TextureContainer.m_TextureIDMap | std::views::values)
		{
			statistics.m_ReloadingAssetCount += texture->m_IsReloading ? 1u : 0u;
		}
		return statistics;
	}

	AssetOwnerId AssetManager::RegisterAssetOwner(std::string label) noexcept
	{
		return m_AssetInterestTracker.RegisterOwner(std::move(label));
	}

	void AssetManager::UnregisterAssetOwner(AssetOwnerId owner) noexcept
	{
		m_AssetInterestTracker.UnregisterOwner(owner);
	}

	AssetLease AssetManager::AcquireAssetLease(
		AssetOwnerId owner,
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation,
		TaskPriority priority,
		bool internal) noexcept
	{
		if (!owner.IsValid() || priority == TaskPriority::Count)
		{
			return {};
		}
		const AssetLeaseAcquireResult result = m_AssetInterestTracker.AcquireLease(
			owner,
			MakeAssetContentVersion(ToAssetKind(kind), stableId, generation),
			priority,
			internal);
		if (!result.IsValid())
		{
			return {};
		}
		if (kind == AssetInterestKind::Texture)
		{
			m_TextureRegistry->ReviveTextureInterest(
				TextureID{ static_cast<uint32_t>(stableId) },
				generation);
			GGLAB_UNUSED(RequestTextureResidency(
				TextureID{ static_cast<uint32_t>(stableId) },
				generation,
				priority));
		}
		else if (kind == AssetInterestKind::Mesh)
		{
			RequestMeshResidency(
				MeshID{ static_cast<uint32_t>(stableId) },
				generation,
				priority);
		}
		HandleInterestChange(result.m_Change);
		if (kind == AssetInterestKind::Model)
		{
			RequestModelResidency(
				ModelID{ static_cast<uint32_t>(stableId) },
				generation);
		}
		return AssetLease(this, result.m_LeaseToken);
	}

	AssetPublicationRetain AssetManager::AcquirePublicationRetain(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation) noexcept
	{
		if (!m_AssetInterestTracker.AcquirePublicationRetain(
			MakeAssetContentVersion(ToAssetKind(kind), stableId, generation)))
		{
			return {};
		}
		if (kind == AssetInterestKind::Texture)
		{
			m_TextureRegistry->ReviveTextureInterest(
				TextureID{ static_cast<uint32_t>(stableId) },
				generation);
		}
		return AssetPublicationRetain(this, kind, stableId, generation);
	}

	void AssetManager::ReleasePublicationRetain(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation) noexcept
	{
		m_AssetInterestTracker.ReleasePublicationRetain(
			MakeAssetContentVersion(ToAssetKind(kind), stableId, generation));
	}

	bool AssetManager::HasPublicationRetain(
		AssetKey key,
		uint64_t generation) const noexcept
	{
		return m_AssetInterestTracker.HasPublicationRetain(
			MakeAssetContentVersion(key, generation));
	}

	void AssetManager::ReleaseAssetLease(uint64_t leaseToken) noexcept
	{
		const std::optional<AssetInterestChange> change =
			m_AssetInterestTracker.ReleaseLease(leaseToken);
		if (!change)
		{
			return;
		}
		if (change->m_IsActive)
		{
			HandleInterestChange(*change);
			return;
		}

		if (change->m_ContentVersion.m_Key.m_Kind == AssetKind::Model)
		{
			ReleaseModelDependencyInterests(ModelID{ static_cast<uint32_t>(
				change->m_ContentVersion.m_Key.m_StableId) });
		}
		CancelAssetIfUnreferenced(
			change->m_ContentVersion.m_Key,
			change->m_ContentVersion.m_ContentGeneration);
	}

	void AssetManager::UpdateAssetLeasePriority(
		uint64_t leaseToken,
		TaskPriority priority) noexcept
	{
		const std::optional<AssetInterestChange> change =
			m_AssetInterestTracker.UpdateLeasePriority(leaseToken, priority);
		if (change)
		{
			HandleInterestChange(*change);
		}
	}

	void AssetManager::HandleInterestChange(const AssetInterestChange& change) noexcept
	{
		if (!change.m_IsActive ||
			(change.m_WasActive && !change.EffectivePriorityChanged()))
		{
			return;
		}
		ApplyInterestPriority(
			change.m_ContentVersion.m_Key,
			change.m_ContentVersion.m_ContentGeneration,
			change.m_EffectivePriority);
		if (change.EffectivePriorityChanged() &&
			change.m_ContentVersion.m_Key.m_Kind == AssetKind::Model)
		{
			UpdateModelDependencyPriorities(
				ModelID{ static_cast<uint32_t>(
					change.m_ContentVersion.m_Key.m_StableId) },
				change.m_EffectivePriority);
		}
	}

	void AssetManager::ApplyInterestPriority(
		AssetKey key,
		uint64_t generation,
		TaskPriority priority) noexcept
	{
		if (key.m_Kind == AssetKind::Model)
		{
			const ModelID modelId{ static_cast<uint32_t>(key.m_StableId) };
			GGLAB_UNUSED(m_AssetLoadCoordinator.UpdateModelImportPriority(
				MakeAssetContentVersion(key, generation),
				priority));
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(modelId, generation),
				priority));
		}
		else if (key.m_Kind == AssetKind::Texture)
		{
			m_TextureRegistry->UpdateTextureLoadPriority(
				TextureID{ static_cast<uint32_t>(key.m_StableId) },
				generation,
				priority);
		}
		else
		{
			const MeshID meshId{ static_cast<uint32_t>(key.m_StableId) };
			if (const Mesh* mesh = GetMesh(meshId);
				mesh && mesh->m_SourceModelId.IsValid())
			{
				if (const Model* sourceModel = GetModel(mesh->m_SourceModelId))
				{
					GGLAB_UNUSED(m_AssetLoadCoordinator.UpdateMeshReloadPriority(
						MakeAssetContentVersion(
							mesh->m_SourceModelId,
							sourceModel->m_ContentGeneration),
						priority));
				}
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(meshId, generation),
				priority));
		}
	}

	TaskPriority AssetManager::GetEffectivePriority(
		AssetKey key,
		TaskPriority fallback) const noexcept
	{
		return m_AssetInterestTracker.GetEffectivePriority(key, fallback);
	}

	bool AssetManager::HasActiveInterest(AssetKey key) const noexcept
	{
		return m_AssetInterestTracker.HasActiveInterest(key);
	}

	bool AssetManager::HasPinnedDependentModel(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation) const noexcept
	{
		const std::span<const AssetContentVersion> dependents =
			m_AssetDependencyGraph.FindDependents(MakeAssetContentVersion(
				ToAssetKind(kind),
				stableId,
				generation));
		return std::ranges::any_of(
			dependents,
			[this](AssetContentVersion dependent) noexcept
			{
				if (dependent.m_Key.m_Kind != AssetKind::Model)
				{
					return false;
				}
				const Model* model = GetModel(ModelID{ static_cast<uint32_t>(
					dependent.m_Key.m_StableId) });
				return model &&
					model->m_ContentGeneration == dependent.m_ContentGeneration &&
					model->m_ResidencyPolicy == AssetResidencyPolicy::Pinned;
			});
	}

	void AssetManager::RefreshModelDependencyInterests(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		const AssetKey modelKey = MakeAssetKey(modelId);
		if (!HasActiveInterest(modelKey) || m_ModelDependencyLeaseTokens.contains(modelId))
		{
			return;
		}
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation || model->m_MeshInstance.empty())
		{
			return;
		}

		const AssetOwnerId owner = RegisterAssetOwner(
			std::format("Model {} dependencies", modelId.Value()));
		m_ModelDependencyOwners.emplace(modelId, owner);
		auto& tokens = m_ModelDependencyLeaseTokens[modelId];
		const TaskPriority priority = GetEffectivePriority(modelKey);
		std::unordered_set<MeshID> meshIds;
		std::unordered_set<TextureID> textureIds;
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			meshIds.insert(instance.m_MeshId);
			if (const Material* material = GetMaterial(instance.m_MaterialId))
			{
				for (TextureID textureId : GetMaterialTextureIds(*material))
				{
					if (textureId.IsValid() && !IsReservedTextureId(textureId))
					{
						textureIds.insert(textureId);
					}
				}
			}
		}

		const auto retainLeaseToken = [&tokens](AssetLease&& lease) noexcept
		{
			if (!lease.IsValid())
			{
				return;
			}
			tokens.push_back(lease.m_LeaseToken);
			lease.m_Manager = nullptr;
			lease.m_LeaseToken = 0;
		};
		for (MeshID meshId : meshIds)
		{
			if (const Mesh* mesh = GetMesh(meshId))
			{
				retainLeaseToken(AcquireAssetLease(
					owner,
					AssetInterestKind::Mesh,
					meshId.Value(),
					mesh->m_ContentGeneration,
					priority,
					true));
			}
		}
		for (TextureID textureId : textureIds)
		{
			if (const Texture* texture = m_TextureRegistry->GetTexture(textureId))
			{
				retainLeaseToken(AcquireAssetLease(
					owner,
					AssetInterestKind::Texture,
					textureId.Value(),
					texture->m_ContentGeneration,
					priority,
					true));
			}
		}
	}

	void AssetManager::ReleaseModelDependencyInterests(ModelID modelId) noexcept
	{
		if (auto leases = m_ModelDependencyLeaseTokens.find(modelId);
			leases != m_ModelDependencyLeaseTokens.end())
		{
			std::vector<uint64_t> tokens = std::move(leases->second);
			m_ModelDependencyLeaseTokens.erase(leases);
			for (uint64_t token : tokens)
			{
				ReleaseAssetLease(token);
			}
		}
		if (const auto owner = m_ModelDependencyOwners.find(modelId);
			owner != m_ModelDependencyOwners.end())
		{
			UnregisterAssetOwner(owner->second);
			m_ModelDependencyOwners.erase(owner);
		}
	}

	void AssetManager::UpdateModelDependencyPriorities(
		ModelID modelId,
		TaskPriority priority) noexcept
	{
		const auto leases = m_ModelDependencyLeaseTokens.find(modelId);
		if (leases == m_ModelDependencyLeaseTokens.end())
		{
			return;
		}
		for (uint64_t token : leases->second)
		{
			UpdateAssetLeasePriority(token, priority);
		}
	}

	void AssetManager::CancelAssetIfUnreferenced(
		AssetKey key,
		uint64_t generation) noexcept
	{
		if (HasPublicationRetain(key, generation))
		{
			++m_PublicationProtectedCancellationCount;
			return;
		}
		if (key.m_Kind == AssetKind::Model)
		{
			CancelModelIfUnreferenced(
				ModelID{ static_cast<uint32_t>(key.m_StableId) },
				generation);
		}
		else if (key.m_Kind == AssetKind::Texture)
		{
			const TextureID textureId{ static_cast<uint32_t>(key.m_StableId) };
			const Texture* texture = m_TextureRegistry->GetTexture(textureId);
			if (!texture || texture->m_ContentGeneration != generation)
			{
				return;
			}
			if (texture->m_State == AssetState::Ready)
			{
				++m_ReadyRetentionCount;
				return;
			}
			if (texture->m_State == AssetState::Queued ||
				texture->m_State == AssetState::LoadingCpu)
			{
				++m_CpuCancellationCount;
			}
			else if (texture->m_Texture.IsValid())
			{
				++m_GpuDeferredCancellationCount;
			}
			else
			{
				++m_ReadyCancellationCount;
			}
			m_TextureRegistry->CancelTextureIfUnreferenced(textureId, generation);
		}
		else
		{
			CancelMeshIfUnreferenced(
				MeshID{ static_cast<uint32_t>(key.m_StableId) },
				generation);
		}
	}

	void AssetManager::CancelModelIfUnreferenced(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		Model* model = EditModel(modelId);
		if (!model || model->m_ContentGeneration != generation || IsTerminalAssetState(model->m_State))
		{
			return;
		}
		if (model->m_State == AssetState::Ready)
		{
			++m_ReadyRetentionCount;
			return;
		}
		if (model->m_ContentState == AssetContentState::Ready &&
			!model->m_MeshInstance.empty() &&
			m_AssetDependencyGraph.FindModel(
				MakeAssetContentVersion(modelId, generation)))
		{
			model->m_CancelRequested = false;
			m_PendingModels.insert(modelId);
			++m_ReadyRetentionCount;
			return;
		}
		model->m_CancelRequested = true;
		GGLAB_UNUSED(m_AssetLoadCoordinator.CancelModelImport(
			MakeAssetContentVersion(modelId, generation)));
		m_AssetLoadCoordinator.DiscardModelImport(MakeAssetKey(modelId));
		const uint32_t cancelledReadyWork = m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(modelId, generation));
		if (model->m_State == AssetState::Queued || model->m_State == AssetState::LoadingCpu)
		{
			++m_CpuCancellationCount;
		}
		else if (cancelledReadyWork > 0)
		{
			++m_ReadyCancellationCount;
		}
		else
		{
			++m_GpuDeferredCancellationCount;
		}
		UnregisterModelDependencies(modelId, generation);
		SetAssetState(*model, AssetState::Cancelled);
		m_PendingModels.erase(modelId);
		ProgressReporter(model->m_LoadProgress).Report(
			0.96f,
			"Model loading cancelled",
			std::format("Model {} has no active owners", modelId.Value()));
	}

	void AssetManager::CancelMeshIfUnreferenced(
		MeshID meshId,
		uint64_t generation) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation || IsTerminalAssetState(mesh->m_State))
		{
			return;
		}
		if (mesh->m_State == AssetState::Ready)
		{
			++m_ReadyRetentionCount;
			return;
		}
		if (mesh->m_IsReloading && !mesh->m_VertexBuffer && !mesh->m_IndexBuffer)
		{
			mesh->m_CancelRequested = true;
			GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
				MakeAssetContentVersion(meshId, generation)));
			mesh->m_IsReloading = false;
			SetMeshState(*mesh, AssetState::CpuReady);
			++m_ReadyCancellationCount;
			return;
		}
		mesh->m_CancelRequested = true;
		const uint32_t cancelledReadyWork = m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(meshId, generation));
		if (mesh->m_VertexBuffer || mesh->m_IndexBuffer)
		{
			++m_GpuDeferredCancellationCount;
		}
		else
		{
			GGLAB_UNUSED(cancelledReadyWork);
			++m_ReadyCancellationCount;
		}
		SetMeshState(
			*mesh,
			mesh->m_VertexBuffer || mesh->m_IndexBuffer ?
				AssetState::GpuProcessing : AssetState::Cancelled);
		ProgressReporter(mesh->m_LoadProgress).Report(
			0.96f,
			mesh->m_VertexBuffer ?
				"Mesh cancellation pending GPU completion" : "Mesh upload cancelled",
			std::format("Mesh {} has no active owners", meshId.Value()));
	}

	AssetManager::ModelLoadRequest AssetManager::LoadModelAsync(
		const std::filesystem::path& path,
		TaskPriority priority) noexcept
	{
		if (path.empty())
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::LoadModelAsync received an empty path.");
			return {};
		}

		const auto canonicalPath = utils::Canonical(path);
		std::error_code errorCode;
		if (!std::filesystem::exists(canonicalPath, errorCode) ||
			!std::filesystem::is_regular_file(canonicalPath, errorCode))
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"AssetManager::LoadModelAsync received a missing model file: '{}'.",
				canonicalPath.string());
			return {};
		}

		if (const ModelID existing = FindModel(canonicalPath); existing.IsValid())
		{
			const Model* model = GetModel(existing);
			if (model && !IsTerminalAssetState(model->m_State))
			{
				return {
					.m_ModelId = existing,
					.m_Generation = model->m_ContentGeneration,
					.m_Task = m_AssetLoadCoordinator.GetModelImportTask(
						MakeAssetContentVersion(existing, model->m_ContentGeneration)),
				};
			}
			GGLAB_UNUSED(DetachTerminalModelPath(canonicalPath, existing));
		}

		const ModelID modelId = CreateModel(canonicalPath, AssetState::Queued);
		Model* model = EditModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		const uint64_t generation = model->m_ContentGeneration;
		model->m_Name = StringID(canonicalPath.filename().generic_string());
		model->m_Type = ModelType::GlTF;

		const AssetLoadSubmission submission =
			m_AssetLoadCoordinator.SubmitModelImport({
				.m_ContentVersion = MakeAssetContentVersion(modelId, generation),
				.m_SourcePath = canonicalPath,
				.m_ImportSettings = m_MaterialTextureSampling,
				.m_Priority = priority,
				.m_Progress = model->m_LoadProgress,
			});
		if (!submission.IsValid())
		{
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import submission failed",
				canonicalPath.filename().generic_string());
			return {
				.m_ModelId = modelId,
				.m_Generation = generation,
			};
		}

		return {
			.m_ModelId = modelId,
			.m_Generation = generation,
			.m_Task = submission.m_Task,
		};
	}

	AssetManager::TextureLoadRequest AssetManager::LoadTextureAsync(
		const std::filesystem::path& path,
		TextureSemantic semantic,
		TaskPriority priority) noexcept
	{
		TextureLoadRequest request =
			m_TextureRegistry->LoadTextureAsync(path, semantic, priority);
		if (request.IsValid())
		{
			if (!request.m_Task.IsValid())
			{
				request.m_Task = RequestTextureResidency(
					request.m_TextureId,
					request.m_Generation,
					priority);
			}
		}
		return request;
	}

	void AssetManager::Tick() noexcept
	{
		DrainLoadCompletions();
		// Batch A: process events that existed before this owner-thread tick.
		DrainStateEvents();
		++m_AssetUsageFrame;
		FinalizeResidencyEvictions();
		const AssetResidencyInventorySnapshot inventory =
			BuildResidencyInventorySnapshot();
		m_LogicalResidentBytes = inventory.m_LogicalResidentBytes;
		AssetResidencyPlan plan = m_AssetResidencyController.BuildPlan(
			inventory,
			m_ResidencyConfig);
		++m_ResidencyPlanningCount;
		m_LastResidencyPlanFrame = plan.m_SnapshotFrame;
		m_LastPlannedResidencyActionCount = static_cast<uint32_t>(plan.m_Actions.size());
		m_LastPlannedResidencyBytes = 0;
		for (const AssetResidencyAction& action : plan.m_Actions)
		{
			m_LastPlannedResidencyBytes += action.m_EstimatedBytes;
		}
		ApplyResidencyPlan(std::move(plan));
		// Batch B: process events emitted while residency work was applied. Events
		// emitted while this batch is processed remain deferred until the next tick.
		DrainStateEvents();
		std::erase_if(m_PendingModels,
			[this](ModelID modelId) noexcept
			{
				return RefreshModelState(modelId);
			});
		m_LastFrameReloadRequestCount =
			std::exchange(m_CurrentFrameReloadRequestCount, 0);
		m_ReloadRequestHighWatermark = std::max(
			m_ReloadRequestHighWatermark,
			m_LastFrameReloadRequestCount);
	}

	AssetResidencyInventorySnapshot AssetManager::BuildResidencyInventorySnapshot() const noexcept
	{
		AssetResidencyInventorySnapshot snapshot{
			.m_Frame = m_AssetUsageFrame,
		};
		snapshot.m_Entries.reserve(
			m_MeshStore.Entries().size() +
			m_TextureRegistry->m_TextureContainer.m_TextureIDMap.size());
		for (const auto& [meshId, mesh] : m_MeshStore.Entries())
		{
			const uint64_t estimatedBytes = EstimateMeshResidentBytes(*mesh);
			if (mesh->m_ResidencyState == AssetResidencyState::Resident ||
				mesh->m_ResidencyState == AssetResidencyState::Evicting)
			{
				snapshot.m_LogicalResidentBytes += estimatedBytes;
			}
			const AssetKey key = MakeAssetKey(meshId);
			snapshot.m_Entries.push_back({
				.m_Stamp = {
					.m_ContentVersion = MakeAssetContentVersion(
						meshId,
						mesh->m_ContentGeneration),
					.m_ResidencyEpoch = mesh->m_ResidencyEpoch,
					.m_LastUsedFrame = mesh->m_LastUsedFrame,
					.m_State = mesh->m_State,
					.m_ContentState = mesh->m_ContentState,
					.m_ResidencyState = mesh->m_ResidencyState,
					.m_ResidencyPolicy = mesh->m_ResidencyPolicy,
				},
				.m_EstimatedBytes = estimatedBytes,
				.m_IsReserved = IsReservedMeshId(meshId),
				.m_HasReloadSource = mesh->m_SourceModelId.IsValid() &&
					mesh->m_SourceMeshIndex != std::numeric_limits<uint32_t>::max(),
				.m_HasActiveInterest = HasActiveInterest(key),
				.m_HasPublicationRetain = HasPublicationRetain(
					key,
					mesh->m_ContentGeneration),
				.m_HasPinnedDependentModel = HasPinnedDependentModel(
					AssetInterestKind::Mesh,
					meshId.Value(),
					mesh->m_ContentGeneration),
			});
		}
		for (const auto& [textureId, texture] :
			m_TextureRegistry->m_TextureContainer.m_TextureIDMap)
		{
			const uint64_t estimatedBytes = EstimateTextureResidentBytes(*texture);
			if (texture->m_ResidencyState == AssetResidencyState::Resident ||
				texture->m_ResidencyState == AssetResidencyState::Evicting)
			{
				snapshot.m_LogicalResidentBytes += estimatedBytes;
			}
			const AssetKey key = MakeAssetKey(textureId);
			snapshot.m_Entries.push_back({
				.m_Stamp = {
					.m_ContentVersion = MakeAssetContentVersion(
						textureId,
						texture->m_ContentGeneration),
					.m_ResidencyEpoch = texture->m_ResidencyEpoch,
					.m_LastUsedFrame = texture->m_LastUsedFrame,
					.m_State = texture->m_State,
					.m_ContentState = texture->m_ContentState,
					.m_ResidencyState = texture->m_ResidencyState,
					.m_ResidencyPolicy = texture->m_ResidencyPolicy,
				},
				.m_EstimatedBytes = estimatedBytes,
				.m_IsReserved = IsReservedTextureId(textureId),
				.m_HasReloadSource = !texture->m_SourcePath.empty(),
				.m_HasActiveInterest = HasActiveInterest(key),
				.m_HasPublicationRetain = HasPublicationRetain(
					key,
					texture->m_ContentGeneration),
				.m_HasPinnedDependentModel = HasPinnedDependentModel(
					AssetInterestKind::Texture,
					textureId.Value(),
					texture->m_ContentGeneration),
			});
		}
		return snapshot;
	}

	void AssetManager::FinalizeResidencyEvictions() noexcept
	{
		for (auto iterator = m_PendingResidencyEvictions.begin();
			iterator != m_PendingResidencyEvictions.end();)
		{
			const PendingResidencyEviction eviction = *iterator;
			if (eviction.m_QuiescedFrame >= m_AssetUsageFrame)
			{
				++iterator;
				continue;
			}

			const AssetKey key = MakeAssetKey(
				ToAssetKind(eviction.m_Kind),
				eviction.m_StableId);
			const bool protectedByInterest = HasActiveInterest(key) ||
				HasPublicationRetain(key, eviction.m_Generation);
			bool finalized = false;
			bool cancelled = protectedByInterest;
			if (eviction.m_Kind == AssetInterestKind::Texture)
			{
				Texture* texture = m_TextureRegistry->GetTexture(
					TextureID{ static_cast<uint32_t>(eviction.m_StableId) });
				if (!texture || texture->m_ContentGeneration != eviction.m_Generation)
				{
					finalized = true;
				}
				else if (texture->m_State != AssetState::Evicting)
				{
					cancelled = texture->m_State == AssetState::Ready;
					finalized = true;
				}
				else if (protectedByInterest ||
					texture->m_ResidencyPolicy == AssetResidencyPolicy::Pinned)
				{
					m_TextureRegistry->SetTextureState(*texture, AssetState::Ready);
					cancelled = true;
					finalized = true;
				}
				else
				{
					finalized = m_TextureRegistry->FinalizeTextureEviction(
						texture->m_Id,
						eviction.m_Generation);
				}
			}
			else if (eviction.m_Kind == AssetInterestKind::Mesh)
			{
				Mesh* mesh = EditMesh(MeshID{ static_cast<uint32_t>(eviction.m_StableId) });
				if (!mesh || mesh->m_ContentGeneration != eviction.m_Generation)
				{
					finalized = true;
				}
				else if (mesh->m_State != AssetState::Evicting)
				{
					cancelled = mesh->m_State == AssetState::Ready;
					finalized = true;
				}
				else if (protectedByInterest ||
					mesh->m_ResidencyPolicy == AssetResidencyPolicy::Pinned)
				{
					SetMeshState(*mesh, AssetState::Ready);
					cancelled = true;
					finalized = true;
				}
				else
				{
					mesh->m_VertexBuffer.Reset();
					mesh->m_IndexBuffer.Reset();
					mesh->m_VertexBufferBinding = {};
					mesh->m_IndexBufferBinding = {};
					mesh->m_IsUploaded = false;
					SetMeshState(*mesh, AssetState::CpuReady);
					ProgressReporter(mesh->m_LoadProgress).Report(
						1.0f,
						"Mesh GPU residency released",
						std::format("Mesh {}", mesh->m_Id.Value()));
					finalized = true;
				}
			}

			if (!finalized)
			{
				++iterator;
				continue;
			}
			if (cancelled)
			{
				++m_ResidencyEvictionCancellationCount;
			}
			else
			{
				++m_ResidencyEvictionCount;
				m_ResidencyEvictedBytes += eviction.m_ResidentBytes;
			}
			iterator = m_PendingResidencyEvictions.erase(iterator);
		}
	}

	void AssetManager::ApplyResidencyPlan(AssetResidencyPlan&& plan) noexcept
	{
		if (plan.m_Consumed || plan.m_SnapshotFrame != m_AssetUsageFrame)
		{
			return;
		}
		plan.m_Consumed = true;
		for (AssetResidencyAction& action : plan.m_Actions)
		{
			if (std::exchange(action.m_Attempted, true) ||
				action.m_Operation != AssetResidencyOperationKind::Evict)
			{
				continue;
			}
			const AssetContentVersion contentVersion = action.m_ExpectedStamp.m_ContentVersion;
			if (contentVersion.m_Key.m_Kind == AssetKind::Texture)
			{
				Texture* texture = m_TextureRegistry->GetTexture(
					TextureID{ static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
				if (!texture ||
					texture->m_ContentGeneration != contentVersion.m_ContentGeneration ||
					texture->m_State != AssetState::Ready)
				{
					continue;
				}
				m_TextureRegistry->SetTextureState(*texture, AssetState::Evicting);
			}
			else if (contentVersion.m_Key.m_Kind == AssetKind::Mesh)
			{
				Mesh* mesh = EditMesh(MeshID{
					static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
				if (!mesh || mesh->m_ContentGeneration != contentVersion.m_ContentGeneration ||
					mesh->m_State != AssetState::Ready)
				{
					continue;
				}
				SetMeshState(*mesh, AssetState::Evicting);
			}
			else
			{
				continue;
			}
			m_PendingResidencyEvictions.push_back({
				.m_Kind = contentVersion.m_Key.m_Kind == AssetKind::Texture ?
					AssetInterestKind::Texture : AssetInterestKind::Mesh,
				.m_StableId = contentVersion.m_Key.m_StableId,
				.m_Generation = contentVersion.m_ContentGeneration,
				.m_ResidentBytes = action.m_EstimatedBytes,
				.m_QuiescedFrame = m_AssetUsageFrame,
			});
		}
	}

	void AssetManager::RequestModelResidency(ModelID modelId, uint64_t generation) noexcept
	{
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation ||
			model->m_MeshInstance.empty())
		{
			return;
		}
		RefreshModelDependencyInterests(modelId, generation);
		if (m_AssetDependencyGraph.FindModel(
			MakeAssetContentVersion(modelId, generation)))
		{
			m_PendingModels.insert(modelId);
		}
	}

	TaskHandle AssetManager::RequestTextureResidency(
		TextureID textureId,
		uint64_t generation,
		TaskPriority priority) noexcept
	{
		Texture* texture = m_TextureRegistry->GetTexture(textureId);
		if (!texture || texture->m_ContentGeneration != generation ||
			(texture->m_State != AssetState::Evicting &&
				(texture->m_State != AssetState::CpuReady ||
					texture->m_ResidencyEpoch == 0)))
		{
			return {};
		}
		++m_ResidencyReloadRequestCount;
		++m_CurrentFrameReloadRequestCount;
		if (texture->m_IsReloading)
		{
			++m_ResidencyReloadCoalescedCount;
		}
		return m_TextureRegistry->RequestTextureResidency(
			textureId,
			generation,
			priority);
	}

	void AssetManager::RequestMeshResidency(
		MeshID meshId,
		uint64_t generation,
		TaskPriority priority) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation)
		{
			return;
		}
		if (mesh->m_State == AssetState::Evicting)
		{
			++m_ResidencyReloadRequestCount;
			++m_CurrentFrameReloadRequestCount;
			SetMeshState(*mesh, AssetState::Ready);
			return;
		}
		if (mesh->m_State != AssetState::CpuReady || mesh->m_ResidencyEpoch == 0 ||
			!mesh->m_SourceModelId.IsValid() ||
			mesh->m_SourceMeshIndex == std::numeric_limits<uint32_t>::max())
		{
			return;
		}
		const Model* sourceModel = GetModel(mesh->m_SourceModelId);
		if (!sourceModel || sourceModel->m_SourcePath.empty())
		{
			return;
		}

		++m_ResidencyReloadRequestCount;
		++m_CurrentFrameReloadRequestCount;
		if (mesh->m_IsReloading)
		{
			++m_ResidencyReloadCoalescedCount;
			return;
		}
		mesh->m_CancelRequested = false;
		mesh->m_IsReloading = true;
		if (m_AssetLoadCoordinator.HasMeshReload(MakeAssetContentVersion(
			mesh->m_SourceModelId,
			sourceModel->m_ContentGeneration)))
		{
			++m_ResidencyReloadCoalescedCount;
			return;
		}
		QueueMeshResidencyReload(mesh->m_SourceModelId, priority);
	}

	void AssetManager::QueueMeshResidencyReload(
		ModelID sourceModelId,
		TaskPriority priority) noexcept
	{
		const Model* sourceModel = GetModel(sourceModelId);
		if (!sourceModel || sourceModel->m_SourcePath.empty())
		{
			return;
		}
		const std::filesystem::path sourcePath = sourceModel->m_SourcePath;
		const uint64_t sourceGeneration = sourceModel->m_ContentGeneration;
		const AssetLoadSubmission submission = m_AssetLoadCoordinator.SubmitMeshReload({
			.m_SourceModelVersion = MakeAssetContentVersion(
				sourceModelId,
				sourceGeneration),
			.m_SourcePath = sourcePath,
			.m_ImportSettings = m_MaterialTextureSampling,
			.m_Priority = priority,
		});
		if (!submission.IsValid())
		{
			for (const auto& mesh : m_MeshStore.Entries() | std::views::values)
			{
				if (mesh->m_SourceModelId == sourceModelId)
				{
					mesh->m_IsReloading = false;
				}
			}
			return;
		}
	}

	void AssetManager::MarkAssetUsed(AssetLifecycle& lifecycle) noexcept
	{
		if (lifecycle.m_ResidencyState != AssetResidencyState::Resident ||
			m_AssetUsageFrame == 0)
		{
			return;
		}
		if (lifecycle.m_LastUsedFrame != m_AssetUsageFrame)
		{
			lifecycle.m_LastUsedFrame = m_AssetUsageFrame;
			++lifecycle.m_UseCount;
		}
	}

	void AssetManager::MarkModelUsed(ModelID modelId) noexcept
	{
		if (Model* model = EditModel(modelId))
		{
			MarkAssetUsed(*model);
		}
	}

	void AssetManager::MarkMeshUsed(MeshID meshId) noexcept
	{
		if (Mesh* mesh = EditMesh(meshId))
		{
			MarkAssetUsed(*mesh);
		}
	}

	void AssetManager::MarkTextureUsed(TextureID textureId) noexcept
	{
		if (Texture* texture = m_TextureRegistry->GetTexture(textureId))
		{
			MarkAssetUsed(*texture);
		}
	}

	bool AssetManager::SetResidencyPolicy(
		AssetLifecycle& lifecycle,
		AssetResidencyPolicy policy,
		bool isReserved) noexcept
	{
		if (isReserved && policy != AssetResidencyPolicy::Pinned)
		{
			return false;
		}
		lifecycle.m_ResidencyPolicy = policy;
		return true;
	}

	void AssetManager::SetMeshState(Mesh& mesh, AssetState state) noexcept
	{
		const AssetState previousState = mesh.m_State;
		SetAssetState(mesh, state);
		if (previousState != state)
		{
			QueueDependencyStateChange(
				MakeAssetContentVersion(mesh.m_Id, mesh.m_ContentGeneration),
				mesh.m_ContentState,
				mesh.m_ResidencyState);
		}
	}

	void AssetManager::RegisterModelDependencies(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}

		std::vector<DependencyStatus> dependencies;
		uint32_t structuralFailureCount = 0;
		const auto addDependency = [&dependencies](
			AssetContentVersion contentVersion,
			const AssetLifecycle& lifecycle) noexcept
		{
			dependencies.push_back(MakeDependencyStatus(contentVersion, lifecycle));
		};

		if (model->m_MeshInstance.empty())
		{
			++structuralFailureCount;
		}
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			if (const Mesh* mesh = GetMesh(instance.m_MeshId))
			{
				addDependency(
					MakeAssetContentVersion(
						instance.m_MeshId,
						mesh->m_ContentGeneration),
					*mesh);
			}
			else
			{
				++structuralFailureCount;
			}

			const Material* material = GetMaterial(instance.m_MaterialId);
			if (!material)
			{
				++structuralFailureCount;
				continue;
			}
			for (TextureID textureId : GetMaterialTextureIds(*material))
			{
				if (!textureId.IsValid())
				{
					continue;
				}
				if (const Texture* texture = m_TextureRegistry->GetTexture(textureId))
				{
					addDependency(
						MakeAssetContentVersion(
							textureId,
							texture->m_ContentGeneration),
						*texture);
				}
				else
				{
					++structuralFailureCount;
				}
			}
		}

		GGLAB_UNUSED(m_AssetDependencyGraph.RegisterModel(
			MakeAssetContentVersion(modelId, generation),
			dependencies,
			structuralFailureCount));
	}

	void AssetManager::UnregisterModelDependencies(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		m_AssetDependencyGraph.UnregisterModel(
			MakeAssetContentVersion(modelId, generation));
	}

	void AssetManager::QueueDependencyStateChange(
		AssetContentVersion contentVersion,
		AssetContentState contentState,
		AssetResidencyState residencyState) noexcept
	{
		GGLAB_UNUSED(m_AssetStateEventQueue.Push({
			.m_ContentVersion = contentVersion,
			.m_ContentState = contentState,
			.m_ResidencyState = residencyState,
		}));
	}

	bool AssetManager::SetModelResidencyPolicy(
		ModelID modelId,
		AssetResidencyPolicy policy) noexcept
	{
		Model* model = EditModel(modelId);
		if (!model || !SetResidencyPolicy(*model, policy, IsReservedModelId(modelId)))
		{
			return false;
		}
		if (policy != AssetResidencyPolicy::Pinned)
		{
			return true;
		}

		std::unordered_set<MeshID> meshIds;
		std::unordered_set<TextureID> textureIds;
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			meshIds.insert(instance.m_MeshId);
			if (const Material* material = GetMaterial(instance.m_MaterialId))
			{
				for (TextureID textureId : GetMaterialTextureIds(*material))
				{
					if (textureId.IsValid() && !IsReservedTextureId(textureId))
					{
						textureIds.insert(textureId);
					}
				}
			}
		}
		for (MeshID meshId : meshIds)
		{
			if (const Mesh* mesh = GetMesh(meshId))
			{
				RequestMeshResidency(
					meshId,
					mesh->m_ContentGeneration,
					TaskPriority::Normal);
			}
		}
		for (TextureID textureId : textureIds)
		{
			if (const Texture* texture = m_TextureRegistry->GetTexture(textureId))
			{
				GGLAB_UNUSED(RequestTextureResidency(
					textureId,
					texture->m_ContentGeneration,
					TaskPriority::Normal));
			}
		}
		m_PendingModels.insert(modelId);
		return true;
	}

	bool AssetManager::SetMeshResidencyPolicy(
		MeshID meshId,
		AssetResidencyPolicy policy) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || !SetResidencyPolicy(*mesh, policy, IsReservedMeshId(meshId)))
		{
			return false;
		}
		if (policy == AssetResidencyPolicy::Pinned)
		{
			RequestMeshResidency(
				meshId,
				mesh->m_ContentGeneration,
				TaskPriority::Normal);
		}
		return true;
	}

	bool AssetManager::SetTextureResidencyPolicy(
		TextureID textureId,
		AssetResidencyPolicy policy) noexcept
	{
		Texture* texture = m_TextureRegistry->GetTexture(textureId);
		if (!texture || !SetResidencyPolicy(
			*texture,
			policy,
			IsReservedTextureId(textureId)))
		{
			return false;
		}
		if (policy == AssetResidencyPolicy::Pinned)
		{
			GGLAB_UNUSED(RequestTextureResidency(
				textureId,
				texture->m_ContentGeneration,
				TaskPriority::Normal));
		}
		return true;
	}

	Texture* AssetManager::GetTexture(TextureID textureId) noexcept
	{
		return m_TextureRegistry->GetTexture(textureId);
	}

	const Texture* AssetManager::GetTexture(TextureID textureId) const noexcept
	{
		return m_TextureRegistry->GetTexture(textureId);
	}

	Mesh* AssetManager::EditMesh(MeshID meshId) noexcept
	{
		return m_MeshStore.Edit(meshId);
	}

	const Mesh* AssetManager::GetMesh(MeshID meshId) const noexcept
	{
		return m_MeshStore.Find(meshId);
	}

	const Material* AssetManager::GetMaterial(MaterialID materialId) const noexcept
	{
		return m_MaterialStore.Find(materialId);
	}

	Model* AssetManager::EditModel(ModelID modelId) noexcept
	{
		return m_ModelStore.Edit(modelId);
	}

	const Model* AssetManager::GetModel(ModelID modelId) const noexcept
	{
		return m_ModelStore.Find(modelId);
	}

	bool AssetManager::RemoveMesh(MeshID meshId) noexcept
	{
		m_PublicationOrphanedMeshes.erase(meshId);
		return m_MeshStore.Remove(meshId);
	}

	bool AssetManager::RemoveMaterial(MaterialID materialId) noexcept
	{
		return m_MaterialStore.Remove(materialId);
	}

	void AssetManager::RollbackPublicationMesh(
		MeshID meshId,
		uint64_t generation) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation)
		{
			return;
		}

		mesh->m_CancelRequested = true;
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
			MakeAssetContentVersion(meshId, generation)));
		if ((mesh->m_VertexBuffer || mesh->m_IndexBuffer) &&
			mesh->m_State != AssetState::Ready)
		{
			m_PublicationOrphanedMeshes.insert(meshId);
			SetMeshState(*mesh, AssetState::GpuProcessing);
			ProgressReporter(mesh->m_LoadProgress).Report(
				0.96f,
				"Mesh publication rollback pending GPU completion");
			return;
		}

		GGLAB_UNUSED(RemoveMesh(meshId));
	}

	MeshID AssetManager::AddProceduralMesh(
		std::unique_ptr<Mesh>&& mesh,
		MeshUploadData& meshUploadData) noexcept
	{
		GGLAB_ASSERT(mesh);
		const MeshStore::InsertResult insertion = m_MeshStore.Insert(std::move(mesh));
		const MeshID meshId = insertion.m_Id;
		Mesh* storedMesh = EditMesh(meshId);
		if (!meshId.IsValid() || !storedMesh)
		{
			return {};
		}
		if (!insertion.m_Inserted)
		{
			return meshId;
		}

		if (!storedMesh->m_HasBounds)
		{
			ComputeMeshBounds(*storedMesh, meshUploadData.m_VerticesData);
		}
		if (!storedMesh->m_LoadProgress)
		{
			storedMesh->m_LoadProgress = std::make_shared<ProgressChannel>();
		}
		if (storedMesh->m_ContentGeneration == 0)
		{
			BeginAssetContentGeneration(
				*storedMesh,
				1,
				AssetState::CpuReady,
				IsReservedMeshId(meshId) ?
					AssetResidencyPolicy::Pinned : AssetResidencyPolicy::Cacheable);
		}

		meshUploadData.m_MeshId = meshId;
		SetMeshState(*storedMesh, AssetState::CpuReady);
		ProgressReporter(storedMesh->m_LoadProgress).Report(
			0.62f,
			"Procedural mesh CPU data ready",
			std::format(
				"{} vertices, {} indices",
				meshUploadData.m_VerticesData.size(),
				meshUploadData.m_IndicesData.size()));

		if (!QueueMeshUpload(std::move(meshUploadData), TaskPriority::Normal))
		{
			SetMeshState(*storedMesh, AssetState::Failed);
		}

		return meshId;
	}

	MaterialID AssetManager::AddMaterial(std::unique_ptr<Material>&& material) noexcept
	{
		return m_MaterialStore.Insert(std::move(material)).m_Id;
	}

	MaterialID AssetManager::AddProceduralMaterial(
		std::unique_ptr<Material>&& material) noexcept
	{
		return AddMaterial(std::move(material));
	}

	ModelID AssetManager::AddProceduralModel(std::unique_ptr<Model>&& model) noexcept
	{
		GGLAB_ASSERT(model);
		const ModelStore::InsertResult insertion = m_ModelStore.Insert(std::move(model));
		const ModelID modelId = insertion.m_Id;
		Model* storedModel = EditModel(modelId);
		if (!modelId.IsValid() || !storedModel)
		{
			return {};
		}
		if (!insertion.m_Inserted)
		{
			return modelId;
		}

		if (storedModel->m_Type == ModelType::Invalid)
		{
			storedModel->m_Type = ModelType::Procedural;
		}
		if (!storedModel->m_LoadProgress)
		{
			storedModel->m_LoadProgress = std::make_shared<ProgressChannel>();
		}
		if (storedModel->m_ContentGeneration == 0)
		{
			BeginAssetContentGeneration(
				*storedModel,
				1,
				AssetState::CpuReady,
				IsReservedModelId(modelId) ?
					AssetResidencyPolicy::Pinned : AssetResidencyPolicy::Cacheable);
		}
		SetAssetState(*storedModel, AssetState::CpuReady);

		RegisterModelDependencies(modelId, storedModel->m_ContentGeneration);
		m_PendingModels.insert(modelId);
		if (RefreshModelState(modelId))
		{
			m_PendingModels.erase(modelId);
		}

		return modelId;
	}

	uint32_t AssetManager::ResolveSrvIndex(TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		return m_TextureRegistry->ResolveSrvIndex(textureId, fallback);
	}

	MaterialTextureBindingGPU AssetManager::ResolveTextureBinding(const MaterialTextureBinding& binding, ReservedTextureIDIndex fallback, SamplerPreset fallbackSampler) const noexcept
	{
		MaterialTextureBindingGPU bindingGpu{};

		bindingGpu.TextureSamplerBinding.TextureIndex = ResolveSrvIndex(binding.m_TextureId, fallback);
		bindingGpu.TextureSamplerBinding.SamplerIndex = m_SamplerRegistry->ResolveSamplerIndex(binding.m_SamplerId, fallbackSampler);

		bindingGpu.TexCoordIndex = binding.m_TexCoordIndex;
		bindingGpu.Padding = 0;

		return bindingGpu;
	}

	bool AssetManager::UploadMesh(
		const MeshUploadData& uploadData,
		TransferBatch& transferBatch) noexcept
	{
		auto* mesh = EditMesh(uploadData.m_MeshId);
		if (mesh == nullptr)
		{
			GGLAB_ASSERT_MSG(false, "UploadMesh: Invalid MeshID, check it!");
			return false;
		}
		SetMeshState(*mesh, AssetState::UploadQueued);

		const auto& verticesData = uploadData.m_VerticesData;
		const auto& indicesData = uploadData.m_IndicesData;

		const auto vertexCount = static_cast<uint32_t>(verticesData.size());
		const auto indexCount = static_cast<uint32_t>(indicesData.size());
		ProgressReporter(mesh->m_LoadProgress).Report(
			0.68f,
			"Recording mesh buffer upload",
			std::format("{} vertices, {} indices", vertexCount, indexCount));

		mesh->m_VertexCount = vertexCount;
		mesh->m_IndexCount = indexCount;

		const auto vertexBufferSize = static_cast<uint64_t>(vertexCount) * sizeof(Vertex);
		const auto indexBufferSize = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);

		if (vertexBufferSize == 0 || indexBufferSize == 0)
		{
			SetMeshState(*mesh, AssetState::Failed);
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::UploadMesh received an empty mesh.");
			return false;
		}
		if (vertexBufferSize > std::numeric_limits<uint32_t>::max() ||
			indexBufferSize > std::numeric_limits<uint32_t>::max())
		{
			SetMeshState(*mesh, AssetState::Failed);
			GGLAB_LOG_GRAPHICS_ERROR("AssetManager::UploadMesh mesh buffers exceed RHI binding size limits.");
			return false;
		}

		RHIBufferDesc vertexBufferDesc{};
		vertexBufferDesc.m_SizeInBytes = vertexBufferSize;
		vertexBufferDesc.m_StrideInBytes = sizeof(Vertex);
		vertexBufferDesc.m_Usage = RHIBufferUsage::Vertex | RHIBufferUsage::CopyDest;

		RHIBufferDesc indexBufferDesc{};
		indexBufferDesc.m_SizeInBytes = indexBufferSize;
		indexBufferDesc.m_StrideInBytes = sizeof(uint32_t);
		indexBufferDesc.m_Usage = RHIBufferUsage::Index | RHIBufferUsage::CopyDest;

		const std::string_view meshName = mesh->m_Name.Name().empty() ?
			std::string_view("UnnamedMesh") : mesh->m_Name.Name();
		const RHIResourceDebugIdentityDesc vertexDebugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = "Mesh.VertexBuffer",
			.m_Label = meshName,
			.m_StableId = uploadData.m_MeshId.Value(),
		};
		const RHIResourceDebugIdentityDesc indexDebugIdentity
		{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = "Mesh.IndexBuffer",
			.m_Label = meshName,
			.m_StableId = uploadData.m_MeshId.Value(),
		};
		const RHIBufferHandle vertexBuffer =
			m_Device->CreateBuffer(vertexBufferDesc, vertexDebugIdentity);
		const RHIBufferHandle indexBuffer =
			m_Device->CreateBuffer(indexBufferDesc, indexDebugIdentity);
		if (!vertexBuffer.IsValid() || !indexBuffer.IsValid())
		{
			SetMeshState(*mesh, AssetState::Failed);
			if (vertexBuffer.IsValid())
			{
				m_Device->DestroyBuffer(vertexBuffer);
			}
			if (indexBuffer.IsValid())
			{
				m_Device->DestroyBuffer(indexBuffer);
			}

			GGLAB_LOG_GRAPHICS_ERROR("AssetManager::UploadMesh failed to create RHI mesh buffers.");
			return false;
		}

		mesh->m_VertexBuffer = RHIBufferOwner(m_Device, vertexBuffer);
		mesh->m_IndexBuffer = RHIBufferOwner(m_Device, indexBuffer);

		const bool vertexUploadSucceeded = transferBatch.UploadBuffer(
			mesh->m_VertexBuffer.Get(), 0, verticesData.data(), vertexBufferSize);
		const bool indexUploadSucceeded = transferBatch.UploadBuffer(
			mesh->m_IndexBuffer.Get(), 0, indicesData.data(), indexBufferSize);
		GGLAB_ASSERT_MSG(vertexUploadSucceeded && indexUploadSucceeded,
			"AssetManager failed to record mesh buffer uploads.");
		if (!vertexUploadSucceeded || !indexUploadSucceeded)
		{
			SetMeshState(*mesh, AssetState::Failed);
			return false;
		}

		mesh->m_VertexBufferBinding.m_Buffer = mesh->m_VertexBuffer.Get();
		mesh->m_VertexBufferBinding.m_Offset = 0;
		mesh->m_VertexBufferBinding.m_Stride = sizeof(Vertex);
		mesh->m_VertexBufferBinding.m_SizeInBytes = static_cast<uint32_t>(vertexBufferSize);

		mesh->m_IndexBufferBinding.m_Buffer = mesh->m_IndexBuffer.Get();
		mesh->m_IndexBufferBinding.m_Offset = 0;
		mesh->m_IndexBufferBinding.m_SizeInBytes = static_cast<uint32_t>(indexBufferSize);
		mesh->m_IndexBufferBinding.m_Format = RHIFormat::R32Uint;

		SetMeshState(*mesh, AssetState::GpuProcessing);
		return true;
	}

	bool AssetManager::QueueMeshUpload(
		MeshUploadData&& uploadData,
		TaskPriority priority) noexcept
	{
		Mesh* mesh = EditMesh(uploadData.m_MeshId);
		if (!mesh || uploadData.m_VerticesData.empty() || uploadData.m_IndicesData.empty())
		{
			return false;
		}

		const MeshID meshId = uploadData.m_MeshId;
		const uint64_t generation = mesh->m_ContentGeneration;
		const AssetStreamingWorkEstimate estimate = EstimateMeshUpload(uploadData);
		if (mesh->m_State != AssetState::Publishing)
		{
			SetMeshState(*mesh, AssetState::CpuReady);
		}
		ProgressReporter(mesh->m_LoadProgress).Report(
			0.62f,
			"Waiting for mesh upload admission",
			std::format("{} bytes", estimate.m_StagingBytes));
		auto payload = std::make_shared<MeshUploadData>(std::move(uploadData));
		m_AssetUploadScheduler->EnqueueUploadRecording(
			{
				.m_Name = std::format("Mesh {}", meshId.Value()),
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Mesh,
					.m_StableId = meshId.Value(),
					.m_Generation = generation,
				},
				.m_Estimate = estimate,
				.m_Priority = priority,
				.m_Progress = mesh->m_LoadProgress,
			},
			[this, meshId, generation, estimate, priority, payload]() mutable noexcept
			{
				Mesh* currentMesh = EditMesh(meshId);
				if (!currentMesh || currentMesh->m_ContentGeneration != generation)
				{
					return;
				}
				if (currentMesh->m_CancelRequested)
				{
					SetMeshState(*currentMesh, AssetState::Cancelled);
					return;
				}

				GGLAB_UNUSED(m_AssetUploadScheduler->RecordUpload(
					{
						.m_Name = std::format("Mesh {}", meshId.Value()),
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Mesh,
							.m_StableId = meshId.Value(),
							.m_Generation = generation,
						},
						.m_Estimate = estimate,
						.m_Priority = priority,
						.m_Progress = currentMesh->m_LoadProgress,
					},
					[this, payload](TransferBatch& batch) noexcept
					{
						return UploadMesh(*payload, batch);
					},
					[this, meshId, generation](const AssetUploadCompletionInfo& completion) noexcept
					{
						const Mesh* currentMesh = GetMesh(meshId);
						if (!currentMesh || currentMesh->m_ContentGeneration != generation)
						{
							return;
						}
						CompleteMeshUpload(
							meshId,
							completion.m_Status == AssetUploadStatus::Succeeded);
					}));
			});
		return true;
	}

	void AssetManager::CompleteMeshUpload(MeshID meshId, bool succeeded) noexcept
	{
		auto* mesh = EditMesh(meshId);
		if (!mesh)
		{
			return;
		}
		const bool residencyReload = mesh->m_IsReloading;
		const bool publicationOrphan = m_PublicationOrphanedMeshes.contains(meshId);
		const bool cancelled = mesh->m_CancelRequested || publicationOrphan;
		const bool publishSucceeded = succeeded && !cancelled;
		SetMeshState(
			*mesh,
			cancelled ?
				(residencyReload ? AssetState::CpuReady : AssetState::Cancelled) :
				(publishSucceeded ? AssetState::Ready :
					(residencyReload ? AssetState::CpuReady : AssetState::Failed)));
		ProgressReporter(mesh->m_LoadProgress).Report(
			publishSucceeded ? 1.0f : 0.96f,
			publishSucceeded ? "Mesh ready" :
				(cancelled ? "Mesh upload cancelled" : "Mesh GPU upload failed"),
			std::format("Mesh {}", meshId.Value()));
		mesh->m_IsUploaded = publishSucceeded;
		mesh->m_IsReloading = false;
		if (residencyReload)
		{
			mesh->m_CancelRequested = false;
		}
		if (!publishSucceeded)
		{
			mesh->m_VertexBuffer.Reset();
			mesh->m_IndexBuffer.Reset();
			mesh->m_VertexBufferBinding = {};
			mesh->m_IndexBufferBinding = {};
		}
		if (publicationOrphan)
		{
			GGLAB_UNUSED(RemoveMesh(meshId));
		}
	}

	void AssetManager::RouteModelImportCompletion(
		AssetOperationToken operation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentModelImport(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		const ModelID modelId{ static_cast<uint32_t>(
			operation.m_ContentVersion.m_Key.m_StableId) };
		const Model* model = GetModel(modelId);
		if (!model ||
			model->m_ContentGeneration != operation.m_ContentVersion.m_ContentGeneration ||
			model->m_CancelRequested)
		{
			m_AssetLoadCoordinator.CompleteModelImport(operation);
			return;
		}

		auto payload = std::make_shared<ImportedModel>(std::move(importedModel));
		m_AssetUploadScheduler->EnqueueCpuPayload(
			{
				.m_Name = completion.m_Name,
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Model,
					.m_StableId = modelId.Value(),
					.m_Generation = operation.m_ContentVersion.m_ContentGeneration,
				},
				.m_Estimate = EstimateImportedModel(*payload),
				.m_Priority = completion.m_Priority,
				.m_Progress = model->m_LoadProgress,
			},
			[this, operation, completion, payload]() mutable noexcept
			{
				CompleteModelLoad(
					operation,
					completion,
					std::move(*payload));
			});
	}

	void AssetManager::RouteMeshReloadCompletion(
		AssetOperationToken operation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentMeshReload(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		const ModelID sourceModelId{ static_cast<uint32_t>(
			operation.m_ContentVersion.m_Key.m_StableId) };
		const Model* sourceModel = GetModel(sourceModelId);
		if (!sourceModel || sourceModel->m_ContentGeneration !=
			operation.m_ContentVersion.m_ContentGeneration)
		{
			m_AssetLoadCoordinator.CompleteMeshReload(operation);
			return;
		}

		auto payload = std::make_shared<ImportedModel>(std::move(importedModel));
		m_AssetUploadScheduler->EnqueueCpuPayload(
			{
				.m_Name = completion.m_Name,
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Model,
					.m_StableId = sourceModelId.Value(),
					.m_Generation = operation.m_ContentVersion.m_ContentGeneration,
				},
				.m_Estimate = EstimateImportedModel(*payload),
				.m_Priority = completion.m_Priority,
			},
			[this, operation, completion, payload]() mutable noexcept
			{
				CompleteMeshReload(
					operation,
					completion,
					std::move(*payload));
			});
	}

	void AssetManager::CompleteModelLoad(
		AssetOperationToken operation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentModelImport(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		m_AssetLoadCoordinator.CompleteModelImport(operation);
		const ModelID modelId{ static_cast<uint32_t>(
			operation.m_ContentVersion.m_Key.m_StableId) };
		const uint64_t generation = operation.m_ContentVersion.m_ContentGeneration;
		Model* model = EditModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}
		if (model->m_CancelRequested)
		{
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import cancelled",
				completion.m_Name);
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import cancelled",
				completion.m_Name);
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import failed",
				completion.m_Error);
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async model import '{}' failed: {}",
				completion.m_Name,
				completion.m_Error);
			return;
		}

		SetAssetState(*model, AssetState::CpuReady);
		ProgressReporter(model->m_LoadProgress).Report(
			0.62f,
			"Queued for resource publication",
			completion.m_Name);
		auto payload = std::make_unique<ImportedModel>(std::move(importedModel));
		const AssetStreamingWorkEstimate estimate = EstimateImportedModel(*payload);
		const TaskPriority priority = GetEffectivePriority(
			MakeAssetKey(modelId),
			completion.m_Priority);
		auto publicationJob = std::make_unique<ModelPublicationJob>(
			CreateModelPublicationServices(),
			MakeAssetContentVersion(modelId, generation),
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds,
			std::move(*payload));
		m_AssetUploadScheduler->EnqueueResourcePublication(
			{
				.m_Name = std::format("Model {}", modelId.Value()),
				.m_Identity = {
					.m_Kind = AssetStreamingWorkKind::Model,
					.m_StableId = modelId.Value(),
					.m_Generation = generation,
				},
				.m_Estimate = estimate,
				.m_Priority = priority,
				.m_Progress = model->m_LoadProgress,
			},
			std::move(publicationJob));
	}

	void AssetManager::CompleteMeshReload(
		AssetOperationToken operation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentMeshReload(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		m_AssetLoadCoordinator.CompleteMeshReload(operation);
		const ModelID sourceModelId{ static_cast<uint32_t>(
			operation.m_ContentVersion.m_Key.m_StableId) };
		const Model* sourceModel = GetModel(sourceModelId);
		if (!sourceModel || sourceModel->m_ContentGeneration !=
			operation.m_ContentVersion.m_ContentGeneration)
		{
			return;
		}

		for (const auto& [meshId, meshOwner] : m_MeshStore.Entries())
		{
			Mesh* mesh = meshOwner.get();
			if (!mesh->m_IsReloading || mesh->m_SourceModelId != sourceModelId)
			{
				continue;
			}
			if (completion.m_Status != TaskStatus::Succeeded ||
				mesh->m_SourceMeshIndex >= importedModel.m_Meshes.size())
			{
				mesh->m_IsReloading = false;
				SetMeshState(*mesh, AssetState::CpuReady);
				continue;
			}
			ImportedMesh& importedMesh = importedModel.m_Meshes[mesh->m_SourceMeshIndex];
			MeshUploadData uploadData{
				.m_MeshId = meshId,
				.m_VerticesData = std::move(importedMesh.m_Vertices),
				.m_IndicesData = std::move(importedMesh.m_Indices),
			};
			if (!QueueMeshUpload(
				std::move(uploadData),
				GetEffectivePriority(MakeAssetKey(meshId), completion.m_Priority)))
			{
				mesh->m_IsReloading = false;
				SetMeshState(*mesh, AssetState::CpuReady);
			}
		}
	}

	MeshID AssetManager::CreateMesh() noexcept
	{
		const MeshID meshId = m_MeshStore.Create();
		Mesh* mesh = EditMesh(meshId);
		GGLAB_ASSERT_NOT_NULL(mesh);
		BeginAssetContentGeneration(
			*mesh,
			1,
			AssetState::LoadingCpu);
		mesh->m_LoadProgress = std::make_shared<ProgressChannel>();

		return meshId;
	}

	ModelID AssetManager::CreateModel(
		const std::filesystem::path& canonicalPath,
		AssetState initialState) noexcept
	{
		const ModelID modelId = m_ModelStore.Create(canonicalPath);
		Model* model = EditModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		if (!model)
		{
			return {};
		}
		BeginAssetContentGeneration(
			*model,
			1,
			initialState);
		model->m_SourcePath = canonicalPath;
		model->m_LoadProgress = std::make_shared<ProgressChannel>();
		ProgressReporter(model->m_LoadProgress).Report(
			initialState == AssetState::Queued ? 0.05f : 0.0f,
			initialState == AssetState::Queued ?
				"Queued for model import" : "Model entry created",
			canonicalPath.filename().generic_string());
		return modelId;
	}

	ModelID AssetManager::FindModel(const std::filesystem::path& canonicalPath) const noexcept
	{
		return m_ModelStore.FindByPath(canonicalPath);
	}

	bool AssetManager::DetachTerminalModelPath(
		const std::filesystem::path& canonicalPath,
		ModelID modelId) noexcept
	{
		if (!m_ModelStore.DetachPath(canonicalPath, modelId))
		{
			return false;
		}

		m_AssetLoadCoordinator.DiscardModelImport(MakeAssetKey(modelId));
		m_PendingModels.erase(modelId);
		GGLAB_LOG_GRAPHICS_INFO(
			"Detached terminal model {} from cache path '{}' so a later request can retry.",
			modelId.Value(),
			canonicalPath.string());
		return true;
	}

	ModelDependencyOutcome AssetManager::EvaluateModelDependenciesByTraversal(
		const Model& model) const noexcept
	{
		if (model.m_MeshInstance.empty())
		{
			return ModelDependencyOutcome::Failed;
		}

		bool pending = false;
		bool cancelled = false;
		for (const ModelMesh& instance : model.m_MeshInstance)
		{
			const Mesh* mesh = GetMesh(instance.m_MeshId);
			const Material* material = GetMaterial(instance.m_MaterialId);
			if (!mesh || !material || mesh->m_State == AssetState::Failed)
			{
				return ModelDependencyOutcome::Failed;
			}
			if (mesh->m_State == AssetState::Cancelled)
			{
				cancelled = true;
			}
			else if (mesh->m_State != AssetState::Ready)
			{
				pending = true;
			}

			for (TextureID textureId : GetMaterialTextureIds(*material))
			{
				if (!textureId.IsValid())
				{
					continue;
				}
				const Texture* texture = m_TextureRegistry->GetTexture(textureId);
				if (!texture || texture->m_State == AssetState::Failed)
				{
					return ModelDependencyOutcome::Failed;
				}
				if (texture->m_State == AssetState::Cancelled)
				{
					cancelled = true;
				}
				else if (texture->m_State != AssetState::Ready)
				{
					pending = true;
				}
			}
		}

		if (cancelled)
		{
			return ModelDependencyOutcome::Cancelled;
		}
		if (pending)
		{
			return ModelDependencyOutcome::Pending;
		}
		return ModelDependencyOutcome::Ready;
	}

	void AssetManager::VerifyModelDependencyState(
		ModelID modelId,
		uint64_t generation,
		ModelDependencyOutcome traversalOutcome) noexcept
	{
		++m_DependencyValidationCount;
		const AssetContentVersion modelVersion = MakeAssetContentVersion(
			modelId,
			generation);
		const bool hasMatchingState =
			m_AssetDependencyGraph.FindModel(modelVersion) != nullptr;
		const ModelDependencyOutcome eventOutcome = hasMatchingState ?
			m_AssetDependencyGraph.EvaluateModel(modelVersion) :
			ModelDependencyOutcome::Failed;
		if (hasMatchingState && eventOutcome == traversalOutcome)
		{
			return;
		}

		++m_DependencyValidationMismatchCount;
		GGLAB_LOG_GRAPHICS_ERROR(
			"Model {} dependency tracking mismatch (generation={}, graph={}, traversal={}, graphPresent={}).",
			modelId.Value(),
			generation,
			static_cast<uint32_t>(eventOutcome),
			static_cast<uint32_t>(traversalOutcome),
			hasMatchingState);
		GGLAB_ASSERT_MSG(false, "Event-driven model dependency state diverged from traversal.");
	}

	bool AssetManager::RefreshModelState(ModelID modelId) noexcept
	{
		Model* model = EditModel(modelId);
		if (!model)
		{
			return true;
		}
		if (IsTerminalAssetState(model->m_State))
		{
			return true;
		}
		if (model->m_State == AssetState::Queued || model->m_State == AssetState::LoadingCpu)
		{
			return false;
		}
		if (model->m_State == AssetState::Ready)
		{
			const AssetContentVersion modelVersion = MakeAssetContentVersion(
				modelId,
				model->m_ContentGeneration);
			if (!m_AssetDependencyGraph.FindModel(modelVersion) ||
				m_AssetDependencyGraph.EvaluateModel(modelVersion) ==
					ModelDependencyOutcome::Ready)
			{
				return true;
			}
		}

		const ModelDependencyOutcome outcome =
			EvaluateModelDependenciesByTraversal(*model);
		VerifyModelDependencyState(modelId, model->m_ContentGeneration, outcome);
		switch (outcome)
		{
		case ModelDependencyOutcome::Failed:
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress).Report(
				0.96f,
				"Model dependency failed");
			UnregisterModelDependencies(modelId, model->m_ContentGeneration);
			return true;

		case ModelDependencyOutcome::Cancelled:
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress).Report(
				0.96f,
				"Model dependency cancelled");
			UnregisterModelDependencies(modelId, model->m_ContentGeneration);
			return true;

		case ModelDependencyOutcome::Pending:
			SetAssetState(*model, AssetState::GpuProcessing);
			ProgressReporter(model->m_LoadProgress).Report(
				0.82f,
				"Waiting for model GPU dependencies");
			return false;

		case ModelDependencyOutcome::Ready:
			break;
		}

		SetAssetState(*model, AssetState::Ready);
		ProgressReporter(model->m_LoadProgress).Report(
			1.0f,
			"Model ready");
		return true;
	}

	void AssetManager::ComputeMeshBounds(Mesh& mesh, std::span<const Vertex> vertices) noexcept
	{
		if (vertices.empty())
		{
			mesh.m_Aabb = math::Aabb{};
			mesh.m_Sphere = math::Sphere{};
			mesh.m_HasBounds = false;
			return;
		}

		const auto* firstPos = std::addressof(vertices[0].m_Position);
		constexpr size_t stride = sizeof(Vertex);

		mesh.m_Aabb = math::CreateAabbFromPoints(vertices.size(), firstPos, stride);
		mesh.m_Sphere = math::CreateSphere(mesh.m_Aabb);
		//mesh.m_Sphere = math::CreateSphereFromPoints(vertices.size(), firstPos, stride);

		mesh.m_HasBounds = true;
	}

	void AssetManager::SetMaterialTexture(Material& material, MaterialTextureSlot slot, const MaterialTextureBinding& binding) noexcept
	{
		switch (slot)
		{
		case MaterialTextureSlot::BaseColor:
			material.m_BaseColorBinding = binding;
			break;
		case MaterialTextureSlot::MetallicRoughness:
			material.m_MetallicRoughnessBinding = binding;
			break;
		case MaterialTextureSlot::Normal:
			material.m_NormalBinding = binding;
			break;
		case MaterialTextureSlot::Occlusion:
			material.m_OcclusionBinding = binding;
			break;
		case MaterialTextureSlot::Emissive:
			material.m_EmissiveBinding = binding;
			break;
		default:
			GGLAB_UNREACHABLE("Unknown MaterialTextureSlot.");
		}
	}
}
