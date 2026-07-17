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
		struct ModelLoadJob
		{
			ImportedModel m_Model;
		};

		struct MeshResidencyReloadJob
		{
			ImportedModel m_Model;
		};

		[[nodiscard]] bool IsTerminalAssetState(AssetState state) noexcept
		{
			return state == AssetState::Failed || state == AssetState::Cancelled;
		}

		[[nodiscard]] TaskPriority HigherPriority(
			TaskPriority lhs,
			TaskPriority rhs) noexcept
		{
			return static_cast<uint8_t>(lhs) < static_cast<uint8_t>(rhs) ? lhs : rhs;
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
		m_TaskSystem(createInfo.m_TaskSystem),
		m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler),
		m_TextureRegistry(createInfo.m_TextureRegistry),
		m_SamplerRegistry(createInfo.m_SamplerRegistry),
		m_MaterialTextureSampling(createInfo.m_MaterialTextureSampling)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TaskSystem != nullptr, "TaskSystem is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
		GGLAB_ASSERT_MSG(m_TextureRegistry != nullptr, "TextureRegistry is null!");
		GGLAB_ASSERT_MSG(m_SamplerRegistry != nullptr, "SamplerRegistry is null!");
		m_TextureRegistry->SetStateChangeCallback(
			[this](TextureID textureId, uint64_t generation, AssetState state) noexcept
			{
				OnDependencyStateChanged(
					AssetInterestKind::Texture,
					textureId.Value(),
					generation,
					state);
			});
	}

	AssetManager::~AssetManager()
	{
		m_TextureRegistry->SetStateChangeCallback({});
		GGLAB_ASSERT_MSG(
			m_AssetLeases.empty() && m_AssetInterests.empty(),
			"AssetManager destroyed while asset leases are still active.");
		GGLAB_ASSERT_MSG(
			m_ModelDependencyOwners.empty() && m_ModelDependencyLeaseTokens.empty(),
			"AssetManager destroyed while model dependency ownership is still active.");
		GGLAB_ASSERT_MSG(
			m_AssetOwners.empty(),
			"AssetManager destroyed while asset owner scopes are still registered.");
		GGLAB_ASSERT_MSG(
			m_PublicationRetains.empty(),
			"AssetManager destroyed while publication retains are still active.");
		GGLAB_ASSERT_MSG(
			m_PublicationOrphanedMeshes.empty(),
			"AssetManager destroyed while publication mesh rollback is pending GPU completion.");
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
		AssetOwnershipStatistics statistics{};
		statistics.m_OwnerCount = static_cast<uint32_t>(m_AssetOwners.size());
		statistics.m_LeaseCount = static_cast<uint32_t>(m_AssetLeases.size());
		statistics.m_ManagedAssetCount = static_cast<uint32_t>(m_AssetInterests.size());
		statistics.m_PriorityUpdateCount = m_OwnershipPriorityUpdateCount;
		statistics.m_CpuCancellationCount = m_CpuCancellationCount;
		statistics.m_ReadyCancellationCount = m_ReadyCancellationCount;
		statistics.m_GpuDeferredCancellationCount = m_GpuDeferredCancellationCount;
		statistics.m_ReadyRetentionCount = m_ReadyRetentionCount;
		statistics.m_PublicationRetainCount = m_PublicationRetainCount;
		statistics.m_PublicationProtectedCancellationCount =
			m_PublicationProtectedCancellationCount;
		statistics.m_ActiveInterests.reserve(m_AssetInterests.size());
		for (const auto& [key, interest] : m_AssetInterests)
		{
			std::unordered_set<uint64_t> owners;
			for (uint64_t token : interest.m_LeaseTokens)
			{
				if (const auto lease = m_AssetLeases.find(token); lease != m_AssetLeases.end())
				{
					owners.insert(lease->second.m_Owner.m_Value);
				}
			}
			statistics.m_ActiveInterests.push_back({
				.m_Kind = key.m_Kind,
				.m_StableId = key.m_StableId,
				.m_Generation = interest.m_Generation,
				.m_LeaseCount = static_cast<uint32_t>(interest.m_LeaseTokens.size()),
				.m_OwnerCount = static_cast<uint32_t>(owners.size()),
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
		};
		for (const PendingResidencyEviction& eviction : m_PendingResidencyEvictions)
		{
			statistics.m_PendingEvictionBytes += eviction.m_ResidentBytes;
		}
		for (const auto& mesh : m_MeshContainer.m_MeshIDMap | std::views::values)
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
		const AssetOwnerId owner{ m_NextAssetOwnerId++ };
		m_AssetOwners.emplace(owner, std::move(label));
		return owner;
	}

	void AssetManager::UnregisterAssetOwner(AssetOwnerId owner) noexcept
	{
		GGLAB_ASSERT_MSG(
			std::ranges::none_of(m_AssetLeases,
				[owner](const auto& entry) noexcept
				{
					return entry.second.m_Owner == owner;
				}),
			"Asset owner unregistered while leases are still active.");
		m_AssetOwners.erase(owner);
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
		const InterestKey key{ .m_Kind = kind, .m_StableId = stableId };
		const uint64_t token = m_NextAssetLeaseToken++;
		m_AssetLeases.emplace(token, LeaseRecord{
			.m_Key = key,
			.m_Owner = owner,
			.m_Generation = generation,
			.m_Priority = priority,
			.m_IsInternal = internal,
		});
		auto [interest, inserted] = m_AssetInterests.try_emplace(key);
		if (inserted)
		{
			interest->second.m_Generation = generation;
			interest->second.m_EffectivePriority = priority;
		}
		interest->second.m_LeaseTokens.insert(token);
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
		if (inserted)
		{
			ApplyInterestPriority(key, generation, priority);
		}
		else
		{
			RecomputeInterestPriority(key);
		}
		if (kind == AssetInterestKind::Model)
		{
			RequestModelResidency(
				ModelID{ static_cast<uint32_t>(stableId) },
				generation);
		}
		return AssetLease(this, token);
	}

	AssetPublicationRetain AssetManager::AcquirePublicationRetain(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation) noexcept
	{
		const InterestKey key{ .m_Kind = kind, .m_StableId = stableId };
		auto [retain, inserted] = m_PublicationRetains.try_emplace(key);
		if (inserted)
		{
			retain->second.m_Generation = generation;
		}
		else if (retain->second.m_Generation != generation)
		{
			GGLAB_ASSERT_MSG(false, "Publication retain generation mismatch.");
			return {};
		}
		++retain->second.m_Count;
		++m_PublicationRetainCount;
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
		const InterestKey key{ .m_Kind = kind, .m_StableId = stableId };
		const auto retain = m_PublicationRetains.find(key);
		if (retain == m_PublicationRetains.end() ||
			retain->second.m_Generation != generation ||
			retain->second.m_Count == 0)
		{
			GGLAB_ASSERT_MSG(false, "Released an unknown publication retain.");
			return;
		}
		--retain->second.m_Count;
		--m_PublicationRetainCount;
		if (retain->second.m_Count == 0)
		{
			m_PublicationRetains.erase(retain);
		}
	}

	bool AssetManager::HasPublicationRetain(
		const InterestKey& key,
		uint64_t generation) const noexcept
	{
		const auto retain = m_PublicationRetains.find(key);
		return retain != m_PublicationRetains.end() &&
			retain->second.m_Generation == generation &&
			retain->second.m_Count > 0;
	}

	void AssetManager::ReleaseAssetLease(uint64_t leaseToken) noexcept
	{
		const auto lease = m_AssetLeases.find(leaseToken);
		if (lease == m_AssetLeases.end())
		{
			return;
		}
		const LeaseRecord record = lease->second;
		m_AssetLeases.erase(lease);
		const auto interest = m_AssetInterests.find(record.m_Key);
		if (interest == m_AssetInterests.end())
		{
			return;
		}
		interest->second.m_LeaseTokens.erase(leaseToken);
		if (!interest->second.m_LeaseTokens.empty())
		{
			RecomputeInterestPriority(record.m_Key);
			return;
		}

		if (record.m_Key.m_Kind == AssetInterestKind::Model)
		{
			ReleaseModelDependencyInterests(ModelID{ static_cast<uint32_t>(record.m_Key.m_StableId) });
		}
		m_AssetInterests.erase(interest);
		CancelAssetIfUnreferenced(record.m_Key, record.m_Generation);
	}

	void AssetManager::UpdateAssetLeasePriority(
		uint64_t leaseToken,
		TaskPriority priority) noexcept
	{
		const auto lease = m_AssetLeases.find(leaseToken);
		if (lease == m_AssetLeases.end() || lease->second.m_Priority == priority)
		{
			return;
		}
		lease->second.m_Priority = priority;
		RecomputeInterestPriority(lease->second.m_Key);
	}

	void AssetManager::RecomputeInterestPriority(const InterestKey& key) noexcept
	{
		const auto interest = m_AssetInterests.find(key);
		if (interest == m_AssetInterests.end() || interest->second.m_LeaseTokens.empty())
		{
			return;
		}
		TaskPriority effective = TaskPriority::Background;
		for (uint64_t token : interest->second.m_LeaseTokens)
		{
			if (const auto lease = m_AssetLeases.find(token); lease != m_AssetLeases.end())
			{
				effective = HigherPriority(effective, lease->second.m_Priority);
			}
		}
		if (effective == interest->second.m_EffectivePriority)
		{
			return;
		}
		interest->second.m_EffectivePriority = effective;
		++m_OwnershipPriorityUpdateCount;
		ApplyInterestPriority(key, interest->second.m_Generation, effective);
		if (key.m_Kind == AssetInterestKind::Model)
		{
			UpdateModelDependencyPriorities(
				ModelID{ static_cast<uint32_t>(key.m_StableId) },
				effective);
		}
	}

	void AssetManager::ApplyInterestPriority(
		const InterestKey& key,
		uint64_t generation,
		TaskPriority priority) noexcept
	{
		if (key.m_Kind == AssetInterestKind::Model)
		{
			const ModelID modelId{ static_cast<uint32_t>(key.m_StableId) };
			if (const auto task = m_ModelLoadTasks.find(modelId); task != m_ModelLoadTasks.end())
			{
				GGLAB_UNUSED(m_TaskSystem->UpdatePriority(task->second, priority));
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(modelId, generation),
				priority));
		}
		else if (key.m_Kind == AssetInterestKind::Texture)
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
				if (const auto task = m_MeshReloadTasks.find(mesh->m_SourceModelId);
					task != m_MeshReloadTasks.end())
				{
					GGLAB_UNUSED(m_TaskSystem->UpdatePriority(task->second, priority));
				}
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(meshId, generation),
				priority));
		}
	}

	TaskPriority AssetManager::GetEffectivePriority(
		const InterestKey& key,
		TaskPriority fallback) const noexcept
	{
		const auto interest = m_AssetInterests.find(key);
		return interest != m_AssetInterests.end() ?
			interest->second.m_EffectivePriority : fallback;
	}

	bool AssetManager::HasActiveInterest(const InterestKey& key) const noexcept
	{
		const auto interest = m_AssetInterests.find(key);
		return interest != m_AssetInterests.end() && !interest->second.m_LeaseTokens.empty();
	}

	bool AssetManager::HasPinnedDependentModel(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation) const noexcept
	{
		const DependencyKey dependency{
			.m_Kind = kind,
			.m_StableId = stableId,
			.m_ContentGeneration = generation,
		};
		const auto dependents = m_ReverseDependencyIndex.find(dependency);
		if (dependents == m_ReverseDependencyIndex.end())
		{
			return false;
		}
		return std::ranges::any_of(
			dependents->second,
			[this](const DependentModel& dependent) noexcept
			{
				const Model* model = GetModel(dependent.m_ModelId);
				return model &&
					model->m_ContentGeneration == dependent.m_ContentGeneration &&
					model->m_ResidencyPolicy == AssetResidencyPolicy::Pinned;
			});
	}

	void AssetManager::RefreshModelDependencyInterests(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		const InterestKey modelKey{
			.m_Kind = AssetInterestKind::Model,
			.m_StableId = modelId.Value(),
		};
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
		const InterestKey& key,
		uint64_t generation) noexcept
	{
		if (HasPublicationRetain(key, generation))
		{
			++m_PublicationProtectedCancellationCount;
			return;
		}
		if (key.m_Kind == AssetInterestKind::Model)
		{
			CancelModelIfUnreferenced(
				ModelID{ static_cast<uint32_t>(key.m_StableId) },
				generation);
		}
		else if (key.m_Kind == AssetInterestKind::Texture)
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
		Model* model = GetModel(modelId);
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
			m_ModelDependencyStates.contains(modelId))
		{
			model->m_CancelRequested = false;
			m_PendingModels.insert(modelId);
			++m_ReadyRetentionCount;
			return;
		}
		model->m_CancelRequested = true;
		if (const auto task = m_ModelLoadTasks.find(modelId); task != m_ModelLoadTasks.end())
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(task->second));
		}
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
		Mesh* mesh = GetMesh(meshId);
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
				const auto task = m_ModelLoadTasks.find(existing);
				return {
					.m_ModelId = existing,
					.m_Generation = model->m_ContentGeneration,
					.m_Task = task != m_ModelLoadTasks.end() ? task->second : TaskHandle{},
				};
			}
			GGLAB_UNUSED(DetachTerminalModelPath(canonicalPath, existing));
		}

		const ModelID modelId = CreateModel(canonicalPath, AssetState::Queued);
		Model* model = GetModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		const uint64_t generation = model->m_ContentGeneration;
		model->m_Name = StringID(canonicalPath.filename().generic_string());
		model->m_Type = ModelType::GlTF;

		auto job = std::make_shared<ModelLoadJob>();
		const ModelImportSettings importSettings = m_MaterialTextureSampling;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format("Asset.ModelImport: {}", canonicalPath.filename().generic_string()),
				.m_Priority = priority,
				.m_Progress = model->m_LoadProgress,
			},
			[canonicalPath, importSettings, job, progress = model->m_LoadProgress](
				std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(
					canonicalPath,
					importSettings,
					stopToken,
					ProgressReporter(progress, 0.05f, 0.62f));
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				job->m_Model = std::move(result.m_Model);
				return TaskResult::Success();
			},
			[this, modelId, generation, job, progress = model->m_LoadProgress](
				const TaskCompletionInfo& completion) noexcept
			{
				const Model* currentModel = GetModel(modelId);
				if (!currentModel || currentModel->m_ContentGeneration != generation ||
					currentModel->m_CancelRequested)
				{
					m_ModelLoadTasks.erase(modelId);
					return;
				}
				m_AssetUploadScheduler->EnqueueCpuPayload(
					{
						.m_Name = completion.m_Name,
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Model,
							.m_StableId = modelId.Value(),
							.m_Generation = generation,
						},
						.m_Estimate = EstimateImportedModel(job->m_Model),
						.m_Priority = completion.m_Priority,
						.m_Progress = progress,
					},
					[this, modelId, generation, completion, job]() mutable noexcept
					{
						CompleteModelLoad(
							modelId,
							generation,
							completion,
							std::move(job->m_Model));
					});
			});
		if (!task.IsValid())
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

		m_ModelLoadTasks.emplace(modelId, task);
		return {
			.m_ModelId = modelId,
			.m_Generation = generation,
			.m_Task = task,
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
		++m_AssetUsageFrame;
		FinalizeResidencyEvictions();
		std::erase_if(m_PendingModels,
			[this](ModelID modelId) noexcept
			{
				return RefreshModelState(modelId);
			});
		m_LogicalResidentBytes = ComputeLogicalResidentBytes();
		SelectResidencyEvictions();
		m_LastFrameReloadRequestCount =
			std::exchange(m_CurrentFrameReloadRequestCount, 0);
		m_ReloadRequestHighWatermark = std::max(
			m_ReloadRequestHighWatermark,
			m_LastFrameReloadRequestCount);
	}

	uint64_t AssetManager::ComputeLogicalResidentBytes() const noexcept
	{
		uint64_t bytes = 0;
		for (const auto& mesh : m_MeshContainer.m_MeshIDMap | std::views::values)
		{
			if (mesh->m_ResidencyState == AssetResidencyState::Resident ||
				mesh->m_ResidencyState == AssetResidencyState::Evicting)
			{
				bytes += EstimateMeshResidentBytes(*mesh);
			}
		}
		for (const auto& texture :
			m_TextureRegistry->m_TextureContainer.m_TextureIDMap | std::views::values)
		{
			if (texture->m_ResidencyState == AssetResidencyState::Resident ||
				texture->m_ResidencyState == AssetResidencyState::Evicting)
			{
				bytes += EstimateTextureResidentBytes(*texture);
			}
		}
		return bytes;
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

			const InterestKey key{
				.m_Kind = eviction.m_Kind,
				.m_StableId = eviction.m_StableId,
			};
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
				Mesh* mesh = GetMesh(MeshID{ static_cast<uint32_t>(eviction.m_StableId) });
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

	void AssetManager::SelectResidencyEvictions() noexcept
	{
		if (!m_ResidencyConfig.m_EnableAutomaticEviction ||
			m_ResidencyConfig.m_MaxEvictionsPerFrame == 0 ||
			m_LogicalResidentBytes <= m_ResidencyConfig.m_HighWatermarkBytes)
		{
			return;
		}

		struct Candidate
		{
			AssetInterestKind m_Kind = AssetInterestKind::Texture;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
			uint64_t m_LastUsedFrame = 0;
			uint64_t m_ResidentBytes = 0;
		};
		std::vector<Candidate> candidates;
		const auto isEligible = [this](
			AssetInterestKind kind,
			uint64_t stableId,
			const AssetLifecycle& lifecycle) noexcept
			{
				const uint64_t unusedFrames = lifecycle.m_LastUsedFrame == 0 ?
					m_AssetUsageFrame : m_AssetUsageFrame - lifecycle.m_LastUsedFrame;
				const InterestKey key{ .m_Kind = kind, .m_StableId = stableId };
				return lifecycle.m_ResidencyPolicy == AssetResidencyPolicy::Cacheable &&
					lifecycle.m_ResidencyState == AssetResidencyState::Resident &&
					unusedFrames >= m_ResidencyConfig.m_MinUnusedFrames &&
					!HasActiveInterest(key) &&
					!HasPinnedDependentModel(
						kind,
						stableId,
						lifecycle.m_ContentGeneration) &&
					!HasPublicationRetain(key, lifecycle.m_ContentGeneration);
			};

		for (const auto& [meshId, mesh] : m_MeshContainer.m_MeshIDMap)
		{
			if (!mesh->m_SourceModelId.IsValid() ||
				mesh->m_SourceMeshIndex == std::numeric_limits<uint32_t>::max() ||
				!isEligible(AssetInterestKind::Mesh, meshId.Value(), *mesh))
			{
				continue;
			}
			candidates.push_back({
				.m_Kind = AssetInterestKind::Mesh,
				.m_StableId = meshId.Value(),
				.m_Generation = mesh->m_ContentGeneration,
				.m_LastUsedFrame = mesh->m_LastUsedFrame,
				.m_ResidentBytes = EstimateMeshResidentBytes(*mesh),
			});
		}
		for (const auto& [textureId, texture] :
			m_TextureRegistry->m_TextureContainer.m_TextureIDMap)
		{
			if (IsReservedTextureId(textureId) || texture->m_SourcePath.empty() ||
				!isEligible(AssetInterestKind::Texture, textureId.Value(), *texture))
			{
				continue;
			}
			candidates.push_back({
				.m_Kind = AssetInterestKind::Texture,
				.m_StableId = textureId.Value(),
				.m_Generation = texture->m_ContentGeneration,
				.m_LastUsedFrame = texture->m_LastUsedFrame,
				.m_ResidentBytes = EstimateTextureResidentBytes(*texture),
			});
		}
		std::ranges::sort(candidates,
			[](const Candidate& lhs, const Candidate& rhs) noexcept
			{
				return std::tie(lhs.m_LastUsedFrame, lhs.m_Kind, lhs.m_StableId) <
					std::tie(rhs.m_LastUsedFrame, rhs.m_Kind, rhs.m_StableId);
			});

		uint64_t projectedBytes = m_LogicalResidentBytes;
		uint32_t selectedCount = 0;
		for (const Candidate& candidate : candidates)
		{
			if (projectedBytes <= m_ResidencyConfig.m_LowWatermarkBytes ||
				selectedCount >= m_ResidencyConfig.m_MaxEvictionsPerFrame)
			{
				break;
			}
			if (candidate.m_Kind == AssetInterestKind::Texture)
			{
				Texture* texture = m_TextureRegistry->GetTexture(
					TextureID{ static_cast<uint32_t>(candidate.m_StableId) });
				if (!texture || texture->m_State != AssetState::Ready)
				{
					continue;
				}
				m_TextureRegistry->SetTextureState(*texture, AssetState::Evicting);
			}
			else
			{
				Mesh* mesh = GetMesh(MeshID{ static_cast<uint32_t>(candidate.m_StableId) });
				if (!mesh || mesh->m_State != AssetState::Ready)
				{
					continue;
				}
				SetMeshState(*mesh, AssetState::Evicting);
			}
			m_PendingResidencyEvictions.push_back({
				.m_Kind = candidate.m_Kind,
				.m_StableId = candidate.m_StableId,
				.m_Generation = candidate.m_Generation,
				.m_ResidentBytes = candidate.m_ResidentBytes,
				.m_QuiescedFrame = m_AssetUsageFrame,
			});
			projectedBytes = projectedBytes > candidate.m_ResidentBytes ?
				projectedBytes - candidate.m_ResidentBytes : 0;
			++selectedCount;
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
		if (const auto dependencies = m_ModelDependencyStates.find(modelId);
			dependencies != m_ModelDependencyStates.end() &&
			dependencies->second.m_ContentGeneration == generation)
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
		Mesh* mesh = GetMesh(meshId);
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
		if (m_MeshReloadTasks.contains(mesh->m_SourceModelId))
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
		auto job = std::make_shared<MeshResidencyReloadJob>();
		const ModelImportSettings importSettings = m_MaterialTextureSampling;
		const TaskHandle task = m_TaskSystem->Submit(
			{
				.m_Name = std::format(
					"Asset.ModelResidencyReload: {}",
					sourcePath.filename().generic_string()),
				.m_Priority = priority,
			},
			[sourcePath, importSettings, job](std::stop_token stopToken) noexcept
			{
				ModelImportResult result = ModelImporter::Import(
					sourcePath,
					importSettings,
					stopToken);
				if (!result.Succeeded())
				{
					return TaskResult::Failure(std::move(result.m_Error));
				}
				job->m_Model = std::move(result.m_Model);
				job->m_Model.m_Textures.clear();
				job->m_Model.m_Materials.clear();
				job->m_Model.m_MeshInstances.clear();
				return TaskResult::Success();
			},
			[this, sourceModelId, sourceGeneration, job](
				const TaskCompletionInfo& completion) noexcept
			{
				m_AssetUploadScheduler->EnqueueCpuPayload(
					{
						.m_Name = completion.m_Name,
						.m_Identity = {
							.m_Kind = AssetStreamingWorkKind::Model,
							.m_StableId = sourceModelId.Value(),
							.m_Generation = sourceGeneration,
						},
						.m_Estimate = EstimateImportedModel(job->m_Model),
						.m_Priority = completion.m_Priority,
					},
					[this, sourceModelId, completion, job]() mutable noexcept
					{
						m_MeshReloadTasks.erase(sourceModelId);
						for (const auto& [meshId, meshOwner] : m_MeshContainer.m_MeshIDMap)
						{
							Mesh* mesh = meshOwner.get();
							if (!mesh->m_IsReloading || mesh->m_SourceModelId != sourceModelId)
							{
								continue;
							}
							if (completion.m_Status != TaskStatus::Succeeded ||
								mesh->m_SourceMeshIndex >= job->m_Model.m_Meshes.size())
							{
								mesh->m_IsReloading = false;
								SetMeshState(*mesh, AssetState::CpuReady);
								continue;
							}
							ImportedMesh& importedMesh =
								job->m_Model.m_Meshes[mesh->m_SourceMeshIndex];
							MeshUploadData uploadData{
								.m_MeshId = meshId,
								.m_VerticesData = std::move(importedMesh.m_Vertices),
								.m_IndicesData = std::move(importedMesh.m_Indices),
							};
							if (!QueueMeshUpload(
								std::move(uploadData),
								GetEffectivePriority({
									.m_Kind = AssetInterestKind::Mesh,
									.m_StableId = meshId.Value(),
								}, completion.m_Priority)))
							{
								mesh->m_IsReloading = false;
								SetMeshState(*mesh, AssetState::CpuReady);
							}
						}
					});
			});
		if (!task.IsValid())
		{
			for (const auto& mesh : m_MeshContainer.m_MeshIDMap | std::views::values)
			{
				if (mesh->m_SourceModelId == sourceModelId)
				{
					mesh->m_IsReloading = false;
				}
			}
			return;
		}
		m_MeshReloadTasks.emplace(sourceModelId, task);
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
		if (Model* model = GetModel(modelId))
		{
			MarkAssetUsed(*model);
		}
	}

	void AssetManager::MarkMeshUsed(MeshID meshId) noexcept
	{
		if (Mesh* mesh = GetMesh(meshId))
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
			OnDependencyStateChanged(
				AssetInterestKind::Mesh,
				mesh.m_Id.Value(),
				mesh.m_ContentGeneration,
				state);
		}
	}

	void AssetManager::IncrementDependencyCounter(
		ModelDependencyState& state,
		AssetState dependencyState) noexcept
	{
		switch (dependencyState)
		{
		case AssetState::Ready:
			++state.m_ReadyCount;
			break;
		case AssetState::Failed:
			++state.m_FailedCount;
			break;
		case AssetState::Cancelled:
			++state.m_CancelledCount;
			break;
		default:
			++state.m_PendingCount;
			break;
		}
	}

	void AssetManager::DecrementDependencyCounter(
		ModelDependencyState& state,
		AssetState dependencyState) noexcept
	{
		switch (dependencyState)
		{
		case AssetState::Ready:
			GGLAB_ASSERT(state.m_ReadyCount > 0);
			--state.m_ReadyCount;
			break;
		case AssetState::Failed:
			GGLAB_ASSERT(state.m_FailedCount > 0);
			--state.m_FailedCount;
			break;
		case AssetState::Cancelled:
			GGLAB_ASSERT(state.m_CancelledCount > 0);
			--state.m_CancelledCount;
			break;
		default:
			GGLAB_ASSERT(state.m_PendingCount > 0);
			--state.m_PendingCount;
			break;
		}
	}

	AssetManager::ModelDependencyOutcome AssetManager::EvaluateModelDependencyCounters(
		const ModelDependencyState& state) noexcept
	{
		if (state.m_StructuralFailureCount > 0 || state.m_FailedCount > 0)
		{
			return ModelDependencyOutcome::Failed;
		}
		if (state.m_CancelledCount > 0)
		{
			return ModelDependencyOutcome::Cancelled;
		}
		if (state.m_PendingCount > 0)
		{
			return ModelDependencyOutcome::Pending;
		}
		return state.m_ReadyCount > 0 ?
			ModelDependencyOutcome::Ready : ModelDependencyOutcome::Failed;
	}

	void AssetManager::RegisterModelDependencies(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		if (const auto existing = m_ModelDependencyStates.find(modelId);
			existing != m_ModelDependencyStates.end())
		{
			UnregisterModelDependencies(modelId, existing->second.m_ContentGeneration);
		}

		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}

		ModelDependencyState dependencyState{
			.m_ContentGeneration = generation,
		};
		const auto addDependency = [&dependencyState](
			AssetInterestKind kind,
			uint64_t stableId,
			const AssetLifecycle& lifecycle) noexcept
		{
			const DependencyKey key{
				.m_Kind = kind,
				.m_StableId = stableId,
				.m_ContentGeneration = lifecycle.m_ContentGeneration,
			};
			const auto [dependency, inserted] =
				dependencyState.m_DependencyStates.emplace(key, lifecycle.m_State);
			if (inserted)
			{
				IncrementDependencyCounter(dependencyState, dependency->second);
			}
		};

		if (model->m_MeshInstance.empty())
		{
			++dependencyState.m_StructuralFailureCount;
		}
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			if (const Mesh* mesh = GetMesh(instance.m_MeshId))
			{
				addDependency(
					AssetInterestKind::Mesh,
					instance.m_MeshId.Value(),
					*mesh);
			}
			else
			{
				++dependencyState.m_StructuralFailureCount;
			}

			const Material* material = GetMaterial(instance.m_MaterialId);
			if (!material)
			{
				++dependencyState.m_StructuralFailureCount;
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
						AssetInterestKind::Texture,
						textureId.Value(),
						*texture);
				}
				else
				{
					++dependencyState.m_StructuralFailureCount;
				}
			}
		}

		auto [storedState, inserted] =
			m_ModelDependencyStates.emplace(modelId, std::move(dependencyState));
		GGLAB_ASSERT(inserted);
		const DependentModel dependentModel{
			.m_ModelId = modelId,
			.m_ContentGeneration = generation,
		};
		for (const auto& [dependency, state] : storedState->second.m_DependencyStates)
		{
			GGLAB_UNUSED(state);
			auto& dependents = m_ReverseDependencyIndex[dependency];
			GGLAB_ASSERT(std::ranges::find(dependents, dependentModel) == dependents.end());
			dependents.push_back(dependentModel);
		}
		++m_DependencyGraphBuildCount;
	}

	void AssetManager::UnregisterModelDependencies(
		ModelID modelId,
		uint64_t generation) noexcept
	{
		const auto state = m_ModelDependencyStates.find(modelId);
		if (state == m_ModelDependencyStates.end() ||
			state->second.m_ContentGeneration != generation)
		{
			return;
		}

		const DependentModel dependentModel{
			.m_ModelId = modelId,
			.m_ContentGeneration = generation,
		};
		for (const auto& [dependency, dependencyState] : state->second.m_DependencyStates)
		{
			GGLAB_UNUSED(dependencyState);
			const auto reverse = m_ReverseDependencyIndex.find(dependency);
			GGLAB_ASSERT(reverse != m_ReverseDependencyIndex.end());
			if (reverse == m_ReverseDependencyIndex.end())
			{
				continue;
			}
			std::erase(reverse->second, dependentModel);
			if (reverse->second.empty())
			{
				m_ReverseDependencyIndex.erase(reverse);
			}
		}
		m_ModelDependencyStates.erase(state);
	}

	void AssetManager::OnDependencyStateChanged(
		AssetInterestKind kind,
		uint64_t stableId,
		uint64_t generation,
		AssetState state) noexcept
	{
		const DependencyKey dependency{
			.m_Kind = kind,
			.m_StableId = stableId,
			.m_ContentGeneration = generation,
		};
		const auto reverse = m_ReverseDependencyIndex.find(dependency);
		if (reverse == m_ReverseDependencyIndex.end())
		{
			return;
		}

		for (const DependentModel& dependent : reverse->second)
		{
			const auto modelState = m_ModelDependencyStates.find(dependent.m_ModelId);
			if (modelState == m_ModelDependencyStates.end() ||
				modelState->second.m_ContentGeneration != dependent.m_ContentGeneration)
			{
				continue;
			}
			const auto dependencyState =
				modelState->second.m_DependencyStates.find(dependency);
			if (dependencyState == modelState->second.m_DependencyStates.end() ||
				dependencyState->second == state)
			{
				continue;
			}

			DecrementDependencyCounter(modelState->second, dependencyState->second);
			dependencyState->second = state;
			IncrementDependencyCounter(modelState->second, state);
			++modelState->second.m_EventUpdateCount;
			++m_DependencyEventUpdateCount;

			Model* model = GetModel(dependent.m_ModelId);
			if (model && model->m_ContentGeneration == dependent.m_ContentGeneration &&
				model->m_State == AssetState::Ready &&
				EvaluateModelDependencyCounters(modelState->second) !=
					ModelDependencyOutcome::Ready)
			{
				SetAssetState(*model, AssetState::GpuProcessing);
				m_PendingModels.insert(dependent.m_ModelId);
			}
		}
	}

	bool AssetManager::SetModelResidencyPolicy(
		ModelID modelId,
		AssetResidencyPolicy policy) noexcept
	{
		Model* model = GetModel(modelId);
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
		Mesh* mesh = GetMesh(meshId);
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

	Mesh* AssetManager::GetMesh(MeshID meshId) noexcept
	{
		return const_cast<Mesh*>(std::as_const(*this).GetMesh(meshId));
	}

	const Mesh* AssetManager::GetMesh(MeshID meshId) const noexcept
	{
		auto iterator = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iterator != m_MeshContainer.m_MeshIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	Material* AssetManager::GetMaterial(MaterialID materialId) noexcept
	{
		return const_cast<Material*>(std::as_const(*this).GetMaterial(materialId));
	}

	const Material* AssetManager::GetMaterial(MaterialID materialId) const noexcept
	{
		auto iterator = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iterator != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	Model* AssetManager::GetModel(ModelID modelId) noexcept
	{
		return const_cast<Model*>(std::as_const(*this).GetModel(modelId));
	}

	const Model* AssetManager::GetModel(ModelID modelId) const noexcept
	{
		auto iterator = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iterator != m_ModelContainer.m_ModelIDMap.end())
		{
			return iterator->second.get();
		}
		return nullptr;
	}

	bool AssetManager::RemoveMesh(MeshID meshId) noexcept
	{
		m_PublicationOrphanedMeshes.erase(meshId);
		return m_MeshContainer.m_MeshIDMap.erase(meshId) > 0;
	}

	bool AssetManager::RemoveMaterial(MaterialID materialId) noexcept
	{
		return m_MaterialContainer.m_MaterialIDMap.erase(materialId) > 0;
	}

	void AssetManager::RollbackPublicationMesh(
		MeshID meshId,
		uint64_t generation) noexcept
	{
		Mesh* mesh = GetMesh(meshId);
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

	MeshID AssetManager::AddMesh(std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept
	{
		GGLAB_ASSERT(mesh);

		// Assign Mesh ID
		auto meshId = mesh->m_Id;
		if (!meshId.IsValid())
		{
			meshId = m_MeshIdCounter.Acquire();
			mesh->m_Id = meshId;
		}

		// Check if mesh already exists
		const auto iterator = m_MeshContainer.m_MeshIDMap.find(meshId);
		if (iterator != m_MeshContainer.m_MeshIDMap.end())
		{
			return meshId;
		}

		if (!mesh->m_HasBounds)
		{
			ComputeMeshBounds(*mesh, meshUploadData.m_VerticesData);
		}
		if (!mesh->m_LoadProgress)
		{
			mesh->m_LoadProgress = std::make_shared<ProgressChannel>();
		}
		if (mesh->m_ContentGeneration == 0)
		{
			BeginAssetContentGeneration(
				*mesh,
				1,
				AssetState::CpuReady,
				IsReservedMeshId(meshId) ?
					AssetResidencyPolicy::Pinned : AssetResidencyPolicy::Cacheable);
		}

		m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		meshUploadData.m_MeshId = meshId;
		Mesh* storedMesh = GetMesh(meshId);
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
		GGLAB_ASSERT(material);

		auto materialId = material->m_Id;
		if (!materialId.IsValid())
		{
			materialId = m_MaterialIdCounter.Acquire();
			material->m_Id = materialId;
		}

		const auto iterator = m_MaterialContainer.m_MaterialIDMap.find(materialId);
		if (iterator != m_MaterialContainer.m_MaterialIDMap.end())
		{
			return materialId;
		}

		m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::move(material));

		return materialId;
	}

	ModelID AssetManager::AddModel(std::unique_ptr<Model>&& model) noexcept
	{
		GGLAB_ASSERT(model);

		auto modelId = model->m_Id;
		if (!modelId.IsValid())
		{
			modelId = m_ModelIdCounter.Acquire();
			model->m_Id = modelId;
		}

		const auto iterator = m_ModelContainer.m_ModelIDMap.find(modelId);
		if (iterator != m_ModelContainer.m_ModelIDMap.end())
		{
			// This id is already have.
			return modelId;
		}

		if (model->m_Type == ModelType::Invalid)
		{
			model->m_Type = ModelType::Procedural;
		}
		if (!model->m_LoadProgress)
		{
			model->m_LoadProgress = std::make_shared<ProgressChannel>();
		}
		if (model->m_ContentGeneration == 0)
		{
			BeginAssetContentGeneration(
				*model,
				1,
				AssetState::CpuReady,
				IsReservedModelId(modelId) ?
					AssetResidencyPolicy::Pinned : AssetResidencyPolicy::Cacheable);
		}
		SetAssetState(*model, AssetState::CpuReady);

		m_ModelContainer.m_ModelIDMap.emplace(modelId, std::move(model));
		RegisterModelDependencies(modelId, GetModel(modelId)->m_ContentGeneration);
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
		auto* mesh = GetMesh(uploadData.m_MeshId);
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
		Mesh* mesh = GetMesh(uploadData.m_MeshId);
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
				Mesh* currentMesh = GetMesh(meshId);
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
		auto* mesh = GetMesh(meshId);
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

	void AssetManager::CompleteModelLoad(
		ModelID modelId,
		uint64_t generation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}
		m_ModelLoadTasks.erase(modelId);
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
		const TaskPriority priority = GetEffectivePriority({
			.m_Kind = AssetInterestKind::Model,
			.m_StableId = modelId.Value(),
		}, completion.m_Priority);
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

	MeshID AssetManager::CreateMesh() noexcept
	{
		const auto meshId = m_MeshIdCounter.Acquire();
		auto idMeshPair = m_MeshContainer.m_MeshIDMap.emplace(meshId, std::make_unique<Mesh>());
		GGLAB_ASSERT_MSG(idMeshPair.second == true, "Emplace MeshID & meshPtr pair failed.");
		BeginAssetContentGeneration(
			*idMeshPair.first->second,
			1,
			AssetState::LoadingCpu);
		idMeshPair.first->second->m_LoadProgress = std::make_shared<ProgressChannel>();

		return meshId;
	}

	MaterialID AssetManager::CreateMaterial() noexcept
	{
		const auto materialId = m_MaterialIdCounter.Acquire();
		auto idMatPair = m_MaterialContainer.m_MaterialIDMap.emplace(materialId, std::make_unique<Material>());
		GGLAB_ASSERT_MSG(idMatPair.second == true, "Emplace MaterialID & materialPtr pair failed.");

		return materialId;
	}

	ModelID AssetManager::CreateModel(
		const std::filesystem::path& canonicalPath,
		AssetState initialState) noexcept
	{
		const auto modelId = m_ModelIdCounter.Acquire();
		auto pathIdPair = m_ModelContainer.m_PathIDMap.emplace(canonicalPath, modelId);
		GGLAB_ASSERT_MSG(pathIdPair.second == true, "Emplace path & ModelID pair failed.");

		auto idModelPair = m_ModelContainer.m_ModelIDMap.emplace(modelId, std::make_unique<Model>());
		GGLAB_ASSERT_MSG(idModelPair.second == true, "Emplace ModelID & ModelPtr pair failed.");
		BeginAssetContentGeneration(
			*idModelPair.first->second,
			1,
			initialState);
		idModelPair.first->second->m_SourcePath = canonicalPath;
		idModelPair.first->second->m_LoadProgress = std::make_shared<ProgressChannel>();
		ProgressReporter(idModelPair.first->second->m_LoadProgress).Report(
			initialState == AssetState::Queued ? 0.05f : 0.0f,
			initialState == AssetState::Queued ?
				"Queued for model import" : "Model entry created",
			canonicalPath.filename().generic_string());
		return modelId;
	}

	ModelID AssetManager::FindModel(const std::filesystem::path& canonicalPath) const noexcept
	{
		auto& modelPathMap = m_ModelContainer.m_PathIDMap;
		auto iterator = modelPathMap.find(canonicalPath);
		if (iterator != modelPathMap.end())
		{
			return iterator->second;
		}
		return InvalidModelID;
	}

	bool AssetManager::DetachTerminalModelPath(
		const std::filesystem::path& canonicalPath,
		ModelID modelId) noexcept
	{
		auto iterator = m_ModelContainer.m_PathIDMap.find(canonicalPath);
		if (iterator == m_ModelContainer.m_PathIDMap.end() || iterator->second != modelId)
		{
			return false;
		}

		m_ModelContainer.m_PathIDMap.erase(iterator);
		m_ModelLoadTasks.erase(modelId);
		m_PendingModels.erase(modelId);
		GGLAB_LOG_GRAPHICS_INFO(
			"Detached terminal model {} from cache path '{}' so a later request can retry.",
			modelId.Value(),
			canonicalPath.string());
		return true;
	}

	AssetManager::ModelDependencyOutcome AssetManager::EvaluateModelDependenciesByTraversal(
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
		const auto dependencyState = m_ModelDependencyStates.find(modelId);
		const bool hasMatchingState = dependencyState != m_ModelDependencyStates.end() &&
			dependencyState->second.m_ContentGeneration == generation;
		const ModelDependencyOutcome eventOutcome = hasMatchingState ?
			EvaluateModelDependencyCounters(dependencyState->second) :
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
		Model* model = GetModel(modelId);
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
			const auto dependencyState = m_ModelDependencyStates.find(modelId);
			if (dependencyState == m_ModelDependencyStates.end() ||
				dependencyState->second.m_ContentGeneration != model->m_ContentGeneration ||
				EvaluateModelDependencyCounters(dependencyState->second) ==
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
