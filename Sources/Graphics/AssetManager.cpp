#include "Core/Precompiled.h"
#include "Graphics/AssetManager.h"
#include "Core/Task/TaskSystem.h"
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
	}

	class AssetManager::ModelPublicationTransaction final :
		public IResourcePublicationJob
	{
	public:
		enum class Stage : uint8_t
		{
			Textures,
			Materials,
			Meshes,
			MeshInstances,
			FallbackMeshInstances,
			Dependencies,
			Commit,
			ReleaseRetains,
			Finished,
		};

		enum class ClaimOrigin : uint8_t
		{
			Reused,
			Created,
		};

		struct ResourceClaim
		{
			AssetInterestKind m_Kind = AssetInterestKind::Texture;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
			ClaimOrigin m_Origin = ClaimOrigin::Reused;
			AssetPublicationRetain m_Retain;
		};

		struct Dependency
		{
			AssetInterestKind m_Kind = AssetInterestKind::Texture;
			uint64_t m_StableId = 0;
			uint64_t m_Generation = 0;
		};

		ModelPublicationTransaction(
			AssetManager* assetManager,
			ModelID modelId,
			uint64_t generation,
			double importQueueMilliseconds,
			double importExecutionMilliseconds,
			std::unique_ptr<ImportedModel>&& payload) noexcept :
			m_AssetManager(assetManager),
			m_ModelId(modelId),
			m_Generation(generation),
			m_ImportQueueMilliseconds(importQueueMilliseconds),
			m_ImportExecutionMilliseconds(importExecutionMilliseconds),
			m_Source(std::move(*payload)),
			m_TextureIds(m_Source.m_Textures.size()),
			m_MeshIds(m_Source.m_Meshes.size())
		{
			m_MaterialIds.reserve(std::max<size_t>(m_Source.m_Materials.size(), 1));
			m_PendingInstances.reserve(std::max(
				m_Source.m_MeshInstances.size(),
				m_Source.m_Meshes.size()));
			m_Claims.reserve(m_Source.m_Textures.size() + m_Source.m_Meshes.size());
			m_CreatedMaterialIds.reserve(std::max<size_t>(m_Source.m_Materials.size(), 1));
			m_Dependencies.reserve(m_Source.m_Textures.size() + m_Source.m_Meshes.size());
			m_DependencyLeaseTokens.reserve(
				m_Source.m_Textures.size() + m_Source.m_Meshes.size());
		}

		[[nodiscard]] AssetResourcePublicationStepResult Step(
			AssetResourcePublicationContext& context) noexcept override
		{
			if (!m_AssetManager)
			{
				return {
					.m_Status = AssetResourcePublicationStepStatus::Failed,
					.m_Error = "Model publication transaction has no AssetManager",
				};
			}
			const ProgressState progressBefore = CaptureProgressState();
			AssetResourcePublicationStepResult result =
				m_AssetManager->StepModelPublication(*this, context.m_Priority);
			result.m_Usage.m_Stage = PublicationStage(m_LastStepStage);
			if (CaptureProgressState() != progressBefore)
			{
				++m_ProgressToken;
			}
			return result;
		}

		void Abort(
			AssetResourcePublicationContext& context,
			AssetResourcePublicationAbortReason reason) noexcept override
		{
			GGLAB_UNUSED(context);
			if (m_AssetManager)
			{
				m_AssetManager->AbortModelPublication(*this, reason);
			}
		}

		[[nodiscard]] uint64_t GetProgressToken() const noexcept override
		{
			return m_ProgressToken;
		}

		[[nodiscard]] AssetResourcePublicationStage GetCurrentStage() const noexcept override
		{
			Stage stage = m_Stage;
			for (;;)
			{
				switch (stage)
				{
				case Stage::Textures:
					if (m_TextureCursor < m_Source.m_Textures.size())
					{
						return AssetResourcePublicationStage::Textures;
					}
					stage = Stage::Materials;
					break;
				case Stage::Materials:
					if (m_MaterialCursor < m_Source.m_Materials.size() ||
						(m_MaterialIds.empty() && !m_DefaultMaterialCreated))
					{
						return AssetResourcePublicationStage::Materials;
					}
					stage = Stage::Meshes;
					break;
				case Stage::Meshes:
					if (m_MeshCursor < m_Source.m_Meshes.size())
					{
						return AssetResourcePublicationStage::Meshes;
					}
					stage = Stage::MeshInstances;
					break;
				case Stage::MeshInstances:
					if (m_InstanceCursor < m_Source.m_MeshInstances.size())
					{
						return AssetResourcePublicationStage::MeshInstances;
					}
					stage = m_PendingInstances.empty() ?
						Stage::FallbackMeshInstances : Stage::Dependencies;
					break;
				case Stage::FallbackMeshInstances:
					if (m_FallbackInstanceCursor < m_MeshIds.size())
					{
						return AssetResourcePublicationStage::MeshInstances;
					}
					stage = Stage::Dependencies;
					break;
				case Stage::Dependencies:
					if (m_DependencyCursor < m_Dependencies.size())
					{
						return AssetResourcePublicationStage::Dependencies;
					}
					return AssetResourcePublicationStage::Commit;
				case Stage::Commit:
					return AssetResourcePublicationStage::Commit;
				case Stage::ReleaseRetains:
					return AssetResourcePublicationStage::ReleaseRetains;
				case Stage::Finished:
					return AssetResourcePublicationStage::Unknown;
				}
			}
		}

	private:
		friend class AssetManager;
		struct ProgressState
		{
			Stage m_Stage = Stage::Finished;
			size_t m_TextureCursor = 0;
			size_t m_MaterialCursor = 0;
			size_t m_MeshCursor = 0;
			size_t m_InstanceCursor = 0;
			size_t m_FallbackInstanceCursor = 0;
			size_t m_DependencyCursor = 0;
			size_t m_ReleaseRetainCursor = 0;
			bool m_DefaultMaterialCreated = false;
			bool m_Committed = false;

			bool operator==(const ProgressState&) const = default;
		};

		[[nodiscard]] ProgressState CaptureProgressState() const noexcept
		{
			return {
				.m_Stage = m_Stage,
				.m_TextureCursor = m_TextureCursor,
				.m_MaterialCursor = m_MaterialCursor,
				.m_MeshCursor = m_MeshCursor,
				.m_InstanceCursor = m_InstanceCursor,
				.m_FallbackInstanceCursor = m_FallbackInstanceCursor,
				.m_DependencyCursor = m_DependencyCursor,
				.m_ReleaseRetainCursor = m_ReleaseRetainCursor,
				.m_DefaultMaterialCreated = m_DefaultMaterialCreated,
				.m_Committed = m_Committed,
			};
		}

		[[nodiscard]] static AssetResourcePublicationStage PublicationStage(
			Stage stage) noexcept
		{
			switch (stage)
			{
			case Stage::Textures: return AssetResourcePublicationStage::Textures;
			case Stage::Materials: return AssetResourcePublicationStage::Materials;
			case Stage::Meshes: return AssetResourcePublicationStage::Meshes;
			case Stage::MeshInstances:
			case Stage::FallbackMeshInstances:
				return AssetResourcePublicationStage::MeshInstances;
			case Stage::Dependencies: return AssetResourcePublicationStage::Dependencies;
			case Stage::Commit: return AssetResourcePublicationStage::Commit;
			case Stage::ReleaseRetains: return AssetResourcePublicationStage::ReleaseRetains;
			case Stage::Finished: return AssetResourcePublicationStage::Unknown;
			}
			return AssetResourcePublicationStage::Unknown;
		}

		AssetManager* m_AssetManager = nullptr;
		ModelID m_ModelId{};
		uint64_t m_Generation = 0;
		double m_ImportQueueMilliseconds = 0.0;
		double m_ImportExecutionMilliseconds = 0.0;
		ImportedModel m_Source;
		Stage m_Stage = Stage::Textures;
		Stage m_LastStepStage = Stage::Finished;
		uint64_t m_ProgressToken = 0;
		size_t m_TextureCursor = 0;
		size_t m_MaterialCursor = 0;
		size_t m_MeshCursor = 0;
		size_t m_InstanceCursor = 0;
		size_t m_FallbackInstanceCursor = 0;
		size_t m_DependencyCursor = 0;
		size_t m_ReleaseRetainCursor = 0;
		std::vector<TextureID> m_TextureIds;
		std::vector<MaterialID> m_MaterialIds;
		std::vector<MeshID> m_MeshIds;
		std::vector<ModelMesh> m_PendingInstances;
		std::vector<ResourceClaim> m_Claims;
		std::vector<MaterialID> m_CreatedMaterialIds;
		std::vector<Dependency> m_Dependencies;
		std::unordered_set<InterestKey, InterestKeyHash> m_DependencyKeys;
		AssetOwnerId m_DependencyOwner{};
		std::vector<uint64_t> m_DependencyLeaseTokens;
		uint32_t m_QueuedTextureUploads = 0;
		uint32_t m_QueuedMeshUploads = 0;
		bool m_DefaultMaterialCreated = false;
		bool m_Committed = false;
		bool m_Aborted = false;
	};

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
	}

	AssetManager::~AssetManager()
	{
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
			RefreshModelDependencyInterests(ModelID{ static_cast<uint32_t>(stableId) }, generation);
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
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority({
				.m_Kind = AssetStreamingWorkKind::Model,
				.m_StableId = key.m_StableId,
				.m_Generation = generation,
			}, priority));
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
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority({
				.m_Kind = AssetStreamingWorkKind::Mesh,
				.m_StableId = key.m_StableId,
				.m_Generation = generation,
			}, priority));
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
		if (!model || model->m_Generation != generation || model->m_MeshInstance.empty())
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
					mesh->m_Generation,
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
					texture->m_Generation,
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
			if (!texture || texture->m_Generation != generation)
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
		if (!model || model->m_Generation != generation || IsTerminalAssetState(model->m_State))
		{
			return;
		}
		if (model->m_State == AssetState::Ready)
		{
			++m_ReadyRetentionCount;
			return;
		}
		model->m_CancelRequested = true;
		if (const auto task = m_ModelLoadTasks.find(modelId); task != m_ModelLoadTasks.end())
		{
			GGLAB_UNUSED(m_TaskSystem->Cancel(task->second));
		}
		const uint32_t cancelledReadyWork = m_AssetUploadScheduler->CancelReadyWork({
			.m_Kind = AssetStreamingWorkKind::Model,
			.m_StableId = modelId.Value(),
			.m_Generation = generation,
		});
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
		model->m_State = AssetState::Cancelled;
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
		if (!mesh || mesh->m_Generation != generation || IsTerminalAssetState(mesh->m_State))
		{
			return;
		}
		if (mesh->m_State == AssetState::Ready)
		{
			++m_ReadyRetentionCount;
			return;
		}
		mesh->m_CancelRequested = true;
		const uint32_t cancelledReadyWork = m_AssetUploadScheduler->CancelReadyWork({
			.m_Kind = AssetStreamingWorkKind::Mesh,
			.m_StableId = meshId.Value(),
			.m_Generation = generation,
		});
		if (mesh->m_VertexBuffer || mesh->m_IndexBuffer)
		{
			++m_GpuDeferredCancellationCount;
		}
		else
		{
			GGLAB_UNUSED(cancelledReadyWork);
			++m_ReadyCancellationCount;
		}
		mesh->m_State = mesh->m_VertexBuffer || mesh->m_IndexBuffer ?
			AssetState::GpuProcessing : AssetState::Cancelled;
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
					.m_Generation = model->m_Generation,
					.m_Task = task != m_ModelLoadTasks.end() ? task->second : TaskHandle{},
				};
			}
			GGLAB_UNUSED(DetachTerminalModelPath(canonicalPath, existing));
		}

		const ModelID modelId = CreateModel(canonicalPath, AssetState::Queued);
		Model* model = GetModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		const uint64_t generation = model->m_Generation;
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
				if (!currentModel || currentModel->m_Generation != generation ||
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
			model->m_State = AssetState::Failed;
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
		return m_TextureRegistry->LoadTextureAsync(path, semantic, priority);
	}

	void AssetManager::Tick() noexcept
	{
		std::erase_if(m_PendingModels,
			[this](ModelID modelId) noexcept
			{
				return RefreshModelState(modelId);
			});
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
		if (!mesh || mesh->m_Generation != generation)
		{
			return;
		}

		mesh->m_CancelRequested = true;
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork({
			.m_Kind = AssetStreamingWorkKind::Mesh,
			.m_StableId = meshId.Value(),
			.m_Generation = generation,
		}));
		if ((mesh->m_VertexBuffer || mesh->m_IndexBuffer) &&
			mesh->m_State != AssetState::Ready)
		{
			m_PublicationOrphanedMeshes.insert(meshId);
			mesh->m_State = AssetState::GpuProcessing;
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
		if (mesh->m_Generation == 0)
		{
			mesh->m_Generation = 1;
		}

		m_MeshContainer.m_MeshIDMap.emplace(meshId, std::move(mesh));
		meshUploadData.m_MeshId = meshId;
		Mesh* storedMesh = GetMesh(meshId);
		storedMesh->m_State = AssetState::CpuReady;
		ProgressReporter(storedMesh->m_LoadProgress).Report(
			0.62f,
			"Procedural mesh CPU data ready",
			std::format(
				"{} vertices, {} indices",
				meshUploadData.m_VerticesData.size(),
				meshUploadData.m_IndicesData.size()));

		if (!QueueMeshUpload(std::move(meshUploadData), TaskPriority::Normal))
		{
			storedMesh->m_State = AssetState::Failed;
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
		if (model->m_Generation == 0)
		{
			model->m_Generation = 1;
		}
		model->m_State = AssetState::CpuReady;

		m_ModelContainer.m_ModelIDMap.emplace(modelId, std::move(model));
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
		mesh->m_State = AssetState::UploadQueued;

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
			mesh->m_State = AssetState::Failed;
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::UploadMesh received an empty mesh.");
			return false;
		}
		if (vertexBufferSize > std::numeric_limits<uint32_t>::max() ||
			indexBufferSize > std::numeric_limits<uint32_t>::max())
		{
			mesh->m_State = AssetState::Failed;
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
			mesh->m_State = AssetState::Failed;
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
			mesh->m_State = AssetState::Failed;
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

		mesh->m_State = AssetState::GpuProcessing;
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
		const uint64_t generation = mesh->m_Generation;
		const AssetStreamingWorkEstimate estimate = EstimateMeshUpload(uploadData);
		if (mesh->m_State != AssetState::Publishing)
		{
			mesh->m_State = AssetState::CpuReady;
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
				if (!currentMesh || currentMesh->m_Generation != generation)
				{
					return;
				}
				if (currentMesh->m_CancelRequested)
				{
					currentMesh->m_State = AssetState::Cancelled;
					return;
				}

				auto batch = m_TransferManager->BeginBatch();
				const bool recorded = UploadMesh(*payload, batch);
				GGLAB_UNUSED(m_AssetUploadScheduler->Submit(
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
					std::move(batch),
					recorded,
					[this, meshId, generation](const AssetUploadCompletionInfo& completion) noexcept
					{
						const Mesh* currentMesh = GetMesh(meshId);
						if (!currentMesh || currentMesh->m_Generation != generation)
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
		const bool publicationOrphan = m_PublicationOrphanedMeshes.contains(meshId);
		const bool cancelled = mesh->m_CancelRequested || publicationOrphan;
		const bool publishSucceeded = succeeded && !cancelled;
		mesh->m_State = cancelled ? AssetState::Cancelled :
			(publishSucceeded ? AssetState::Ready : AssetState::Failed);
		ProgressReporter(mesh->m_LoadProgress).Report(
			publishSucceeded ? 1.0f : 0.96f,
			publishSucceeded ? "Mesh ready" :
				(cancelled ? "Mesh upload cancelled" : "Mesh GPU upload failed"),
			std::format("Mesh {}", meshId.Value()));
		mesh->m_IsUploaded = publishSucceeded;
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

	AssetResourcePublicationStepResult AssetManager::StepModelPublication(
		ModelPublicationTransaction& transaction,
		TaskPriority priority) noexcept
	{
		using Stage = ModelPublicationTransaction::Stage;
		using ClaimOrigin = ModelPublicationTransaction::ClaimOrigin;

		const auto failed = [](std::string error,
			AssetResourcePublicationStepUsage usage = {}) noexcept
		{
			return AssetResourcePublicationStepResult{
				.m_Status = AssetResourcePublicationStepStatus::Failed,
				.m_Usage = usage,
				.m_Error = std::move(error),
			};
		};
		const auto continued = [](AssetResourcePublicationStepUsage usage = {}) noexcept
		{
			return AssetResourcePublicationStepResult{
				.m_Status = AssetResourcePublicationStepStatus::Continue,
				.m_Usage = usage,
			};
		};
		const auto addDependency = [&transaction](
			AssetInterestKind kind,
			uint64_t stableId,
			uint64_t generation) noexcept
		{
			const InterestKey key{ .m_Kind = kind, .m_StableId = stableId };
			if (transaction.m_DependencyKeys.insert(key).second)
			{
				transaction.m_Dependencies.push_back({
					.m_Kind = kind,
					.m_StableId = stableId,
					.m_Generation = generation,
				});
			}
		};

		if (transaction.m_Aborted)
		{
			return { .m_Status = AssetResourcePublicationStepStatus::Cancelled };
		}
		Model* model = GetModel(transaction.m_ModelId);
		if (!model || model->m_Generation != transaction.m_Generation ||
			model->m_CancelRequested)
		{
			return { .m_Status = AssetResourcePublicationStepStatus::Cancelled };
		}
		if (model->m_State == AssetState::CpuReady)
		{
			model->m_State = AssetState::Publishing;
			ProgressReporter(model->m_LoadProgress).Report(
				0.64f,
				"Publishing model resources incrementally");
		}
		if (transaction.m_Source.m_Meshes.empty())
		{
			return failed("Imported model contains no meshes");
		}

		for (;;)
		{
			transaction.m_LastStepStage = transaction.m_Stage;
			switch (transaction.m_Stage)
			{
			case Stage::Textures:
			{
				if (transaction.m_TextureCursor >= transaction.m_Source.m_Textures.size())
				{
					transaction.m_Stage = Stage::Materials;
					continue;
				}

				const size_t textureIndex = transaction.m_TextureCursor++;
				ImportedTexture& importedTexture =
					transaction.m_Source.m_Textures[textureIndex];
				const uint64_t sourceBytes = static_cast<uint64_t>(
					importedTexture.m_Data.m_Pixels.size());
				TextureID textureId = m_TextureRegistry->FindTexture(
					importedTexture.m_CanonicalPath,
					importedTexture.m_ImportSettings);
				Texture* texture = m_TextureRegistry->GetTexture(textureId);
				if (textureId.IsValid() && !texture)
				{
					GGLAB_UNUSED(m_TextureRegistry->RemoveTexture(textureId));
					textureId.Reset();
				}
				else if (texture && IsTerminalAssetState(texture->m_State))
				{
					const InterestKey textureKey{
						.m_Kind = AssetInterestKind::Texture,
						.m_StableId = textureId.Value(),
					};
					if (HasActiveInterest(textureKey) ||
						HasPublicationRetain(textureKey, texture->m_Generation))
					{
						return failed(
							std::format("Terminal texture {} is still retained", textureId.Value()),
							{ .m_PayloadBytesDestroyed = sourceBytes });
					}
					GGLAB_UNUSED(m_TextureRegistry->RemoveTexture(textureId));
					textureId.Reset();
					texture = nullptr;
				}

				bool created = false;
				if (!textureId.IsValid())
				{
					if (!importedTexture.m_Data.IsValid())
					{
						transaction.m_TextureIds[textureIndex].Reset();
						importedTexture = {};
						return continued({ .m_PayloadBytesDestroyed = sourceBytes });
					}
					textureId = m_TextureRegistry->CreateTexture(
						importedTexture.m_CanonicalPath,
						importedTexture.m_ImportSettings);
					texture = m_TextureRegistry->GetTexture(textureId);
					if (!textureId.IsValid() || !texture)
					{
						importedTexture = {};
						return failed(
							"Failed to create texture entry during model publication",
							{ .m_PayloadBytesDestroyed = sourceBytes });
					}
					created = true;
					texture->m_State = AssetState::Publishing;
				}

				transaction.m_TextureIds[textureIndex] = textureId;
				const uint64_t textureGeneration = texture->m_Generation;
				auto retain = AcquirePublicationRetain(
					AssetInterestKind::Texture,
					textureId.Value(),
					textureGeneration);
				transaction.m_Claims.push_back({
					.m_Kind = AssetInterestKind::Texture,
					.m_StableId = textureId.Value(),
					.m_Generation = textureGeneration,
					.m_Origin = created ? ClaimOrigin::Created : ClaimOrigin::Reused,
					.m_Retain = std::move(retain),
				});
				if (!IsReservedTextureId(textureId))
				{
					addDependency(
						AssetInterestKind::Texture,
						textureId.Value(),
						textureGeneration);
				}

				if (!created)
				{
					importedTexture = {};
					return continued({ .m_PayloadBytesDestroyed = sourceBytes });
				}

				auto uploadData = m_TextureRegistry->MakeTextureUploadData(
					textureId,
					std::move(importedTexture.m_Data),
					importedTexture.m_Semantic);
				const bool queued = m_TextureRegistry->QueueTextureUpload(
					std::move(uploadData),
					priority);
				importedTexture = {};
				if (!queued)
				{
					return failed(
						std::format("Failed to queue texture {} upload", textureId.Value()),
						{
							.m_ResourceCreations = 1,
							.m_PayloadBytesDestroyed = sourceBytes,
						});
				}
				++transaction.m_QueuedTextureUploads;
				return continued({
					.m_ResourceCreations = 1,
					.m_PayloadBytesMovedToUpload = sourceBytes,
				});
			}

			case Stage::Materials:
			{
				ImportedMaterial* importedMaterial = nullptr;
				if (transaction.m_MaterialCursor < transaction.m_Source.m_Materials.size())
				{
					importedMaterial = &transaction.m_Source.m_Materials[
						transaction.m_MaterialCursor++];
				}
				else if (transaction.m_MaterialIds.empty() &&
					!transaction.m_DefaultMaterialCreated)
				{
					transaction.m_DefaultMaterialCreated = true;
				}
				else
				{
					transaction.m_Stage = Stage::Meshes;
					continue;
				}

				auto material = std::make_unique<Material>();
				if (importedMaterial)
				{
					static_cast<MaterialProperties&>(*material) =
						importedMaterial->m_Properties;
					material->m_Name = StringID(importedMaterial->m_Name);
					for (uint32_t slotIndex = 0;
						slotIndex < utils::ToIndex(MaterialTextureSlot::Count);
						++slotIndex)
					{
						const ImportedMaterialTextureBinding& importedBinding =
							importedMaterial->m_TextureBindings[slotIndex];
						if (importedBinding.m_TextureIndex ==
							ImportedMaterialTextureBinding::InvalidTextureIndex)
						{
							continue;
						}

						MaterialTextureBinding binding{};
						if (importedBinding.m_TextureIndex < transaction.m_TextureIds.size())
						{
							binding.m_TextureId = transaction.m_TextureIds[
								importedBinding.m_TextureIndex];
						}
						binding.m_SamplerId = m_SamplerRegistry->GetOrCreateSampler(
							importedBinding.m_SamplerKey);
						binding.m_TexCoordIndex = importedBinding.m_TexCoordIndex;
						SetMaterialTexture(
							*material,
							static_cast<MaterialTextureSlot>(slotIndex),
							binding);
					}
				}

				const MaterialID materialId = AddMaterial(std::move(material));
				if (!materialId.IsValid())
				{
					return failed("Failed to create material during model publication");
				}
				transaction.m_MaterialIds.push_back(materialId);
				transaction.m_CreatedMaterialIds.push_back(materialId);
				if (importedMaterial)
				{
					*importedMaterial = {};
				}
				return continued({ .m_ResourceCreations = 1 });
			}

			case Stage::Meshes:
			{
				if (transaction.m_MeshCursor >= transaction.m_Source.m_Meshes.size())
				{
					transaction.m_Stage = Stage::MeshInstances;
					continue;
				}

				const size_t meshIndex = transaction.m_MeshCursor++;
				ImportedMesh& importedMesh = transaction.m_Source.m_Meshes[meshIndex];
				const uint64_t vertexBytes = static_cast<uint64_t>(
					importedMesh.m_Vertices.size()) * sizeof(Vertex);
				const uint64_t indexBytes = static_cast<uint64_t>(
					importedMesh.m_Indices.size()) * sizeof(uint32_t);
				const uint64_t sourceBytes = vertexBytes + indexBytes;
				const MeshID meshId = CreateMesh();
				Mesh* mesh = GetMesh(meshId);
				if (!meshId.IsValid() || !mesh)
				{
					return failed(
						"Failed to create mesh entry during model publication",
						{ .m_PayloadBytesDestroyed = sourceBytes });
				}

				transaction.m_MeshIds[meshIndex] = meshId;
				mesh->m_Id = meshId;
				mesh->m_Name = StringID(importedMesh.m_Name);
				mesh->m_Sphere = importedMesh.m_Sphere;
				mesh->m_Aabb = importedMesh.m_Aabb;
				mesh->m_HasBounds = importedMesh.m_HasBounds;
				mesh->m_State = AssetState::Publishing;
				auto retain = AcquirePublicationRetain(
					AssetInterestKind::Mesh,
					meshId.Value(),
					mesh->m_Generation);
				transaction.m_Claims.push_back({
					.m_Kind = AssetInterestKind::Mesh,
					.m_StableId = meshId.Value(),
					.m_Generation = mesh->m_Generation,
					.m_Origin = ClaimOrigin::Created,
					.m_Retain = std::move(retain),
				});
				addDependency(
					AssetInterestKind::Mesh,
					meshId.Value(),
					mesh->m_Generation);

				MeshUploadData uploadData{};
				uploadData.m_MeshId = meshId;
				uploadData.m_VerticesData = std::move(importedMesh.m_Vertices);
				uploadData.m_IndicesData = std::move(importedMesh.m_Indices);
				importedMesh.m_Name.clear();
				const bool queued = QueueMeshUpload(std::move(uploadData), priority);
				if (!queued)
				{
					return failed(
						std::format("Failed to queue mesh {} upload", meshId.Value()),
						{
							.m_ResourceCreations = 1,
							.m_PayloadBytesDestroyed = sourceBytes,
						});
				}
				++transaction.m_QueuedMeshUploads;
				return continued({
					.m_ResourceCreations = 1,
					.m_PayloadBytesMovedToUpload = sourceBytes,
				});
			}

			case Stage::MeshInstances:
			{
				if (transaction.m_InstanceCursor >=
					transaction.m_Source.m_MeshInstances.size())
				{
					transaction.m_Stage = transaction.m_PendingInstances.empty() ?
						Stage::FallbackMeshInstances : Stage::Dependencies;
					continue;
				}

				ImportedModelMesh& importedInstance =
					transaction.m_Source.m_MeshInstances[transaction.m_InstanceCursor++];
				if (importedInstance.m_MeshIndex >= transaction.m_MeshIds.size())
				{
					importedInstance = {};
					return continued();
				}
				const uint32_t materialIndex =
					importedInstance.m_MaterialIndex < transaction.m_MaterialIds.size() ?
					importedInstance.m_MaterialIndex : 0;
				transaction.m_PendingInstances.push_back({
					.m_MeshId = transaction.m_MeshIds[importedInstance.m_MeshIndex],
					.m_MaterialId = transaction.m_MaterialIds[materialIndex],
					.m_LocalTransform = importedInstance.m_LocalTransform,
				});
				importedInstance = {};
				return continued({ .m_ResourceCreations = 1 });
			}

			case Stage::FallbackMeshInstances:
			{
				if (transaction.m_FallbackInstanceCursor >= transaction.m_MeshIds.size())
				{
					transaction.m_Stage = Stage::Dependencies;
					continue;
				}
				const size_t meshIndex = transaction.m_FallbackInstanceCursor++;
				const uint32_t sourceMaterialIndex =
					transaction.m_Source.m_Meshes[meshIndex].m_MaterialIndex;
				const uint32_t materialIndex =
					sourceMaterialIndex < transaction.m_MaterialIds.size() ?
					sourceMaterialIndex : 0;
				transaction.m_PendingInstances.push_back({
					.m_MeshId = transaction.m_MeshIds[meshIndex],
					.m_MaterialId = transaction.m_MaterialIds[materialIndex],
				});
				return continued({ .m_ResourceCreations = 1 });
			}

			case Stage::Dependencies:
			{
				if (transaction.m_DependencyCursor >= transaction.m_Dependencies.size())
				{
					transaction.m_Stage = Stage::Commit;
					continue;
				}
				if (!transaction.m_DependencyOwner.IsValid())
				{
					transaction.m_DependencyOwner = RegisterAssetOwner(
						std::format("Model {} dependencies", transaction.m_ModelId.Value()));
				}

				const ModelPublicationTransaction::Dependency& dependency =
					transaction.m_Dependencies[transaction.m_DependencyCursor++];
				AssetLease lease = AcquireAssetLease(
					transaction.m_DependencyOwner,
					dependency.m_Kind,
					dependency.m_StableId,
					dependency.m_Generation,
					priority,
					true);
				if (!lease.IsValid())
				{
					return failed("Failed to acquire model dependency lease");
				}
				transaction.m_DependencyLeaseTokens.push_back(lease.m_LeaseToken);
				lease.m_Manager = nullptr;
				lease.m_LeaseToken = 0;
				return continued();
			}

			case Stage::Commit:
			{
				if (transaction.m_PendingInstances.empty())
				{
					return failed("Model publication produced no renderable mesh instances");
				}
				if (!transaction.m_DependencyOwner.IsValid())
				{
					transaction.m_DependencyOwner = RegisterAssetOwner(
						std::format("Model {} dependencies", transaction.m_ModelId.Value()));
				}
				if (m_ModelDependencyOwners.contains(transaction.m_ModelId) ||
					m_ModelDependencyLeaseTokens.contains(transaction.m_ModelId))
				{
					return failed("Model already owns dependency interests before publication commit");
				}

				model->m_Name = StringID(transaction.m_Source.m_Name);
				model->m_Type = transaction.m_Source.m_Type;
				model->m_MeshInstance = std::move(transaction.m_PendingInstances);
				m_ModelDependencyOwners.emplace(
					transaction.m_ModelId,
					transaction.m_DependencyOwner);
				m_ModelDependencyLeaseTokens.emplace(
					transaction.m_ModelId,
					std::move(transaction.m_DependencyLeaseTokens));
				transaction.m_DependencyOwner = {};
				model->m_State = AssetState::UploadQueued;
				m_PendingModels.insert(transaction.m_ModelId);
				transaction.m_Committed = true;
				transaction.m_Stage = Stage::ReleaseRetains;
				ProgressReporter(model->m_LoadProgress).Report(
					0.66f,
					"Waiting for model dependency uploads",
					std::format(
						"{} texture uploads, {} mesh uploads",
						transaction.m_QueuedTextureUploads,
						transaction.m_QueuedMeshUploads));
				if (RefreshModelState(transaction.m_ModelId))
				{
					m_PendingModels.erase(transaction.m_ModelId);
				}
				return continued();
			}

			case Stage::ReleaseRetains:
			{
				if (transaction.m_ReleaseRetainCursor < transaction.m_Claims.size())
				{
					transaction.m_Claims[transaction.m_ReleaseRetainCursor++].m_Retain.Reset();
					if (transaction.m_ReleaseRetainCursor < transaction.m_Claims.size())
					{
						return continued();
					}
				}
				transaction.m_Stage = Stage::Finished;
				GGLAB_LOG_GRAPHICS_INFO(
					"Async model {} published incrementally (instances={}, textureUploads={}, meshUploads={}, queueMs={:.2f}, cpuMs={:.2f}).",
					transaction.m_ModelId.Value(),
					model->m_MeshInstance.size(),
					transaction.m_QueuedTextureUploads,
					transaction.m_QueuedMeshUploads,
					transaction.m_ImportQueueMilliseconds,
					transaction.m_ImportExecutionMilliseconds);
				return { .m_Status = AssetResourcePublicationStepStatus::Completed };
			}

			case Stage::Finished:
				return { .m_Status = AssetResourcePublicationStepStatus::Completed };
			}
		}
	}

	void AssetManager::AbortModelPublication(
		ModelPublicationTransaction& transaction,
		AssetResourcePublicationAbortReason reason) noexcept
	{
		if (transaction.m_Aborted)
		{
			return;
		}
		transaction.m_Aborted = true;

		if (transaction.m_Committed)
		{
			for (ModelPublicationTransaction::ResourceClaim& claim : transaction.m_Claims)
			{
				const InterestKey key{
					.m_Kind = claim.m_Kind,
					.m_StableId = claim.m_StableId,
				};
				claim.m_Retain.Reset();
				if (!HasPublicationRetain(key, claim.m_Generation) &&
					!HasActiveInterest(key))
				{
					CancelAssetIfUnreferenced(key, claim.m_Generation);
				}
			}
			return;
		}

		std::vector<uint64_t> dependencyTokens =
			std::move(transaction.m_DependencyLeaseTokens);
		for (uint64_t token : dependencyTokens)
		{
			ReleaseAssetLease(token);
		}
		if (transaction.m_DependencyOwner.IsValid())
		{
			UnregisterAssetOwner(transaction.m_DependencyOwner);
			transaction.m_DependencyOwner = {};
		}

		for (auto claim = transaction.m_Claims.rbegin();
			claim != transaction.m_Claims.rend();
			++claim)
		{
			const InterestKey key{
				.m_Kind = claim->m_Kind,
				.m_StableId = claim->m_StableId,
			};
			claim->m_Retain.Reset();
			if (HasPublicationRetain(key, claim->m_Generation) || HasActiveInterest(key))
			{
				continue;
			}

			if (claim->m_Origin == ModelPublicationTransaction::ClaimOrigin::Created)
			{
				if (claim->m_Kind == AssetInterestKind::Texture)
				{
					m_TextureRegistry->RollbackPublicationTexture(
						TextureID{ static_cast<uint32_t>(claim->m_StableId) },
						claim->m_Generation);
				}
				else if (claim->m_Kind == AssetInterestKind::Mesh)
				{
					RollbackPublicationMesh(
						MeshID{ static_cast<uint32_t>(claim->m_StableId) },
						claim->m_Generation);
				}
			}
			else
			{
				CancelAssetIfUnreferenced(key, claim->m_Generation);
			}
		}

		for (MaterialID materialId : transaction.m_CreatedMaterialIds)
		{
			GGLAB_UNUSED(RemoveMaterial(materialId));
		}

		Model* model = GetModel(transaction.m_ModelId);
		if (model && model->m_Generation == transaction.m_Generation)
		{
			model->m_MeshInstance.clear();
			model->m_State = reason == AssetResourcePublicationAbortReason::Failed ?
				AssetState::Failed : AssetState::Cancelled;
			m_PendingModels.erase(transaction.m_ModelId);
			ProgressReporter(model->m_LoadProgress).Report(
				0.62f,
				reason == AssetResourcePublicationAbortReason::Failed ?
					"Model publication failed" : "Model publication cancelled");
		}
	}

	void AssetManager::CompleteModelLoad(
		ModelID modelId,
		uint64_t generation,
		const TaskCompletionInfo& completion,
		ImportedModel&& importedModel) noexcept
	{
		Model* model = GetModel(modelId);
		if (!model || model->m_Generation != generation)
		{
			return;
		}
		m_ModelLoadTasks.erase(modelId);
		if (model->m_CancelRequested)
		{
			model->m_State = AssetState::Cancelled;
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import cancelled",
				completion.m_Name);
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			model->m_State = AssetState::Cancelled;
			ProgressReporter(model->m_LoadProgress).Report(
				0.05f,
				"Model import cancelled",
				completion.m_Name);
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded)
		{
			model->m_State = AssetState::Failed;
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

		model->m_State = AssetState::CpuReady;
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
		auto publicationJob = std::make_unique<ModelPublicationTransaction>(
			this,
			modelId,
			generation,
			completion.m_QueueMilliseconds,
			completion.m_ExecutionMilliseconds,
			std::move(payload));
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
		idMeshPair.first->second->m_State = AssetState::LoadingCpu;
		idMeshPair.first->second->m_Generation = 1;
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
		idModelPair.first->second->m_State = initialState;
		idModelPair.first->second->m_Generation = 1;
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

	bool AssetManager::RefreshModelState(ModelID modelId) noexcept
	{
		Model* model = GetModel(modelId);
		if (!model)
		{
			return true;
		}
		if (model->m_State == AssetState::Ready || IsTerminalAssetState(model->m_State))
		{
			return true;
		}
		if (model->m_State == AssetState::Queued || model->m_State == AssetState::LoadingCpu)
		{
			return false;
		}

		if (model->m_MeshInstance.empty())
		{
			model->m_State = AssetState::Failed;
			ProgressReporter(model->m_LoadProgress).Report(
				0.64f,
				"Model has no renderable mesh instances");
			return true;
		}

		bool pending = false;
		bool cancelled = false;
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			const Mesh* mesh = GetMesh(instance.m_MeshId);
			const Material* material = GetMaterial(instance.m_MaterialId);
			if (!mesh || !material || mesh->m_State == AssetState::Failed)
			{
				model->m_State = AssetState::Failed;
				ProgressReporter(model->m_LoadProgress).Report(
					0.96f,
					"Model dependency failed");
				return true;
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
					model->m_State = AssetState::Failed;
					ProgressReporter(model->m_LoadProgress).Report(
						0.96f,
						"Model texture dependency failed");
					return true;
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
			model->m_State = AssetState::Cancelled;
			ProgressReporter(model->m_LoadProgress).Report(
				0.96f,
				"Model dependency cancelled");
			return true;
		}
		if (pending)
		{
			model->m_State = AssetState::GpuProcessing;
			ProgressReporter(model->m_LoadProgress).Report(
				0.82f,
				"Waiting for model GPU dependencies");
			return false;
		}

		model->m_State = AssetState::Ready;
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
