#include "Graphics/Asset/AssetManager.h"
#include "Core/CoreMacros.h"
#include "Core/Log/LogMacros.h"
#include "Core/Task/TaskSystem.h"
#include "Core/Utility/PathUtils.h"
#include "Core/Utility/TypeUtils.h"
#include "Graphics/Asset/AssetIdentityConversions.h"
#include "Graphics/Asset/Publication/ModelPublicationJob.h"
#include "Graphics/Asset/Streaming/AssetUploadScheduler.h"
#include "Graphics/Asset/TextureAssetSystem.h"
#include "Graphics/RHI/RHIBuffer.h"
#include "Graphics/RHI/RHIDevice.h"
#include "Graphics/TransferManager.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] bool IsTerminalAssetState(AssetState state) noexcept
		{
			return state == AssetState::Failed || state == AssetState::Cancelled;
		}

		[[nodiscard]] bool IsInterestAssetKind(AssetKind kind) noexcept
		{
			return kind == AssetKind::Model || kind == AssetKind::Texture ||
				kind == AssetKind::Mesh;
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
			const uint64_t vertexBytes =
				static_cast<uint64_t>(uploadData.GetVertices().size()) * sizeof(Vertex);
			const uint64_t indexBytes =
				static_cast<uint64_t>(uploadData.GetIndices().size()) * sizeof(uint32_t);
			return {
				.m_SourceBytes = vertexBytes + indexBytes,
				.m_StagingBytes = vertexBytes + indexBytes,
				.m_OperationCount = 2,
			};
		}

		[[nodiscard]] AssetStreamingWorkEstimate EstimateImportedModel(
			const ModelImportArtifact& model) noexcept
		{
			AssetStreamingWorkEstimate estimate{};
			// Referenced texture allocations are accounted by TextureArtifactCache.
			// The model publication payload only owns its mesh source bytes directly.
			for (const ImportedMesh& mesh : model.m_Meshes)
			{
				estimate.m_SourceBytes +=
					static_cast<uint64_t>(mesh.m_Vertices.size()) * sizeof(Vertex);
				estimate.m_SourceBytes +=
					static_cast<uint64_t>(mesh.m_Indices.size()) * sizeof(uint32_t);
			}
			return estimate;
		}

		[[nodiscard]] uint64_t EstimateTextureResidentBytes(const Texture& texture) noexcept
		{
			if (!texture.m_Gpu.m_Texture.IsValid())
			{
				return 0;
			}
			const uint64_t bytesPerTexel =
				GetRHIFormatInfo(texture.m_Gpu.m_Desc.m_Format).m_BytesPerBlock;
			uint64_t bytes = 0;
			for (uint32_t mip = 0; mip < texture.m_Gpu.m_Desc.m_MipLevels; ++mip)
			{
				const uint64_t width =
					std::max<uint32_t>(1, texture.m_Gpu.m_Desc.m_Extent.m_Width >> mip);
				const uint64_t height =
					std::max<uint32_t>(1, texture.m_Gpu.m_Desc.m_Extent.m_Height >> mip);
				const uint64_t depth =
					std::max<uint32_t>(1, texture.m_Gpu.m_Desc.m_Extent.m_Depth >> mip);
				bytes += width * height * depth * bytesPerTexel;
			}
			return bytes * texture.m_Gpu.m_Desc.m_ArraySize * texture.m_Gpu.m_Desc.m_SampleCount;
		}

		[[nodiscard]] uint64_t EstimateMeshResidentBytes(const Mesh& mesh) noexcept
		{
			return static_cast<uint64_t>(mesh.m_VertexBufferBinding.m_SizeInBytes) +
				mesh.m_IndexBufferBinding.m_SizeInBytes;
		}
	}

	AssetManager::AssetManager(const CreateInfo& createInfo) noexcept :
		m_Device(createInfo.m_Device), m_TransferManager(createInfo.m_TransferManager),
		m_AssetUploadScheduler(createInfo.m_AssetUploadScheduler),
		m_TextureArtifactCache(createInfo.m_TextureArtifactCache),
		m_ModelImportArtifactCache(createInfo.m_ModelImportArtifactCache),
		m_AssetLoadCoordinator({
			.m_TaskSystem = createInfo.m_TaskSystem,
			.m_ModelImportArtifactCache = &m_ModelImportArtifactCache,
			.m_TextureArtifactCache = &m_TextureArtifactCache,
			.m_TextureDerivedDataCacheDirectory = createInfo.m_TextureDerivedDataCacheDirectory,
			}),
			m_TextureAssets(std::make_unique<TextureAssetSystem>(TextureAssetSystem::CreateInfo{
				.m_Device = createInfo.m_Device,
				.m_LoadCoordinator = &m_AssetLoadCoordinator,
				.m_TransferManager = createInfo.m_TransferManager,
				.m_AssetUploadScheduler = createInfo.m_AssetUploadScheduler,
				.m_StateEvents = &m_AssetStateEventQueue,
				.m_ArtifactCache = &m_TextureArtifactCache,
				})),
				m_SamplerRegistry(createInfo.m_SamplerRegistry),
				m_MaterialTextureSampling(createInfo.m_MaterialTextureSampling)
	{
		GGLAB_ASSERT_MSG(m_Device != nullptr, "RHIDevice is null!");
		GGLAB_ASSERT_MSG(m_TransferManager != nullptr, "TransferManager is null!");
		GGLAB_ASSERT_MSG(m_AssetUploadScheduler != nullptr, "AssetUploadScheduler is null!");
		GGLAB_ASSERT_MSG(m_SamplerRegistry != nullptr, "SamplerRegistry is null!");
		m_TextureAssets->InitializeReservedTextures();
	}

	AssetManager::~AssetManager()
	{
		GGLAB_ASSERT_MSG(m_IsPreparedForShutdown,
			"AssetManager destroyed without explicit shutdown preparation.");
		GGLAB_ASSERT_MSG(
			!m_AssetInterestTracker.HasLeases() && !m_AssetInterestTracker.HasInterests(),
			"AssetManager destroyed while asset leases are still active.");
		GGLAB_ASSERT_MSG(m_ModelDependencyOwners.empty() && m_ModelDependencyLeaseTokens.empty(),
			"AssetManager destroyed while model dependency ownership is still active.");
		GGLAB_ASSERT_MSG(!m_AssetInterestTracker.HasOwners(),
			"AssetManager destroyed while asset owner scopes are still registered.");
		GGLAB_ASSERT_MSG(!m_AssetInterestTracker.HasPublicationRetains(),
			"AssetManager destroyed while publication retains are still active.");
		GGLAB_ASSERT_MSG(m_PublicationOrphanedMeshes.empty(),
			"AssetManager destroyed while publication mesh rollback is pending GPU completion.");
		GGLAB_ASSERT_MSG(m_PendingRuntimeRetirements.empty(),
			"AssetManager destroyed while runtime entry retirements are pending.");
		GGLAB_ASSERT_MSG(m_PendingResidencyEvictions.empty(),
			"AssetManager destroyed while residency eviction commands are pending.");
		GGLAB_ASSERT_MSG(!m_AssetLoadCoordinator.HasActiveOperations() &&
			!m_AssetLoadCoordinator.HasPendingCompletions(),
			"AssetManager destroyed before asset load operations were drained.");
		GGLAB_ASSERT_MSG(!m_AssetStateEventQueue.HasPendingEvents(),
			"AssetManager destroyed before asset state events were drained.");
	}

	AssetPublicationRetain::AssetPublicationRetain(AssetPublicationRetain&& other) noexcept :
		m_Manager(std::exchange(other.m_Manager, nullptr)),
		m_Kind(std::exchange(other.m_Kind, AssetKind::Model)),
		m_StableId(std::exchange(other.m_StableId, 0)),
		m_Generation(std::exchange(other.m_Generation, 0))
	{
	}

	AssetPublicationRetain& AssetPublicationRetain::operator=(
		AssetPublicationRetain&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Manager = std::exchange(other.m_Manager, nullptr);
			m_Kind = std::exchange(other.m_Kind, AssetKind::Model);
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
			m_Manager->ReleasePublicationRetain(m_Kind, m_StableId, m_Generation);
		}
		m_Manager = nullptr;
		m_Kind = AssetKind::Model;
		m_StableId = 0;
		m_Generation = 0;
	}

	AssetLease::AssetLease(AssetLease&& other) noexcept :
		m_Manager(std::exchange(other.m_Manager, nullptr)),
		m_LeaseToken(std::exchange(other.m_LeaseToken, 0))
	{
	}

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
		m_Owner(std::exchange(other.m_Owner, {})), m_Leases(std::move(other.m_Leases))
	{
	}

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
		const std::filesystem::path& path, TaskPriority priority) noexcept
	{
		if (!m_Manager || !m_Owner.IsValid())
		{
			return {};
		}
		AssetManager::ModelLoadRequest request = m_Manager->LoadModelAsync(path, priority);
		if (request.IsValid())
		{
			m_Leases.emplace_back(m_Manager->AcquireAssetLease(m_Owner, AssetKind::Model,
				request.m_ModelId.Value(), request.m_Generation, priority));
		}
		return request;
	}

	AssetManager::TextureLoadRequest AssetOwnerScope::LoadTextureAsync(
		const std::filesystem::path& path, TextureSemantic semantic, TaskPriority priority) noexcept
	{
		if (!m_Manager || !m_Owner.IsValid())
		{
			return {};
		}
		AssetManager::TextureLoadRequest request =
			m_Manager->LoadTextureAsync(path, semantic, priority);
		if (request.IsValid())
		{
			m_Leases.emplace_back(m_Manager->AcquireAssetLease(m_Owner, AssetKind::Texture,
				request.m_TextureId.Value(), request.m_Generation, priority));
		}
		return request;
	}

	bool AssetOwnerScope::RetainTexture(TextureContentRef content, TaskPriority priority) noexcept
	{
		if (!m_Manager || !m_Owner.IsValid() || !content.IsValid() ||
			priority == TaskPriority::Count || !m_Manager->GetTextureState(content).has_value())
		{
			return false;
		}
		AssetLease lease = m_Manager->AcquireAssetLease(
			m_Owner, AssetKind::Texture, content.m_Id.Value(), content.m_Generation, priority);
		if (!lease.IsValid())
		{
			return false;
		}
		m_Leases.emplace_back(std::move(lease));
		return true;
	}

	void AssetOwnerScope::Reset() noexcept
	{
		m_Leases.clear();
	}

	AssetOwnerScope AssetManager::CreateOwnerScope() noexcept
	{
		if (!m_AcceptingCommands)
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager rejected owner creation after shutdown began.");
			return {};
		}
		return AssetOwnerScope(this, RegisterAssetOwner());
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
		statistics.m_RuntimeRetirementRequestCount = m_RuntimeRetirementRequestCount;
		statistics.m_RuntimeRetirementCancellationCount = m_RuntimeRetirementCancellationCount;
		statistics.m_RuntimeRetirementCount = m_RuntimeRetirementCount;
		statistics.m_PendingRuntimeRetirementCount =
			static_cast<uint32_t>(m_PendingRuntimeRetirements.size());
		statistics.m_PublicationRetainCount = trackerStatistics.m_PublicationRetainCount;
		statistics.m_PublicationProtectedCancellationCount =
			m_PublicationProtectedCancellationCount;
		statistics.m_ActiveInterests.reserve(trackerStatistics.m_ActiveInterests.size());
		for (const TrackedAssetInterestActivity& interest : trackerStatistics.m_ActiveInterests)
		{
			statistics.m_ActiveInterests.push_back({
				.m_Kind = interest.m_ContentVersion.m_Key.m_Kind,
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
							result.m_Operation, result.m_Completion, std::move(result.m_Artifact));
					}
					else if constexpr (std::same_as<Result, ModelImportFailed>)
					{
						RouteModelImportCompletion(result.m_Operation, result.m_Completion, {});
					}
					else if constexpr (std::same_as<Result, MeshReloadSucceeded>)
					{
						RouteMeshReloadCompletion(
							result.m_Operation, result.m_Completion, std::move(result.m_Artifact));
					}
					else if constexpr (std::same_as<Result, MeshReloadFailed>)
					{
						RouteMeshReloadCompletion(result.m_Operation, result.m_Completion, {});
					}
					else if constexpr (std::same_as<Result, TextureDecodeSucceeded>)
					{
						m_TextureAssets->RouteTextureDecodeCompletion(std::move(result));
					}
					else
					{
						m_TextureAssets->RouteTextureDecodeCompletion(std::move(result));
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
			AssetLifecycle* operationLifecycle = nullptr;
			const bool textureOperation =
				event.m_Operation &&
				event.m_Status.m_ContentVersion.m_Key.m_Kind == AssetKind::Texture;
			if (event.m_Operation)
			{
				// Residency events are accepted against the operation that is current at
				// drain time, not merely the operation that was current when queued.
				operationLifecycle = textureOperation ? nullptr
					: FindResidencyLifecycle(
						event.m_Status.m_ContentVersion.m_Key);
				const bool currentOperation =
					textureOperation
					? m_TextureAssets->IsCurrentResidencyOperation(*event.m_Operation)
					: operationLifecycle && AssetResidencyController::IsCurrentOperation(
						*operationLifecycle, *event.m_Operation);
				if (!currentOperation)
				{
					m_AssetResidencyController.RecordStaleStateEvent();
					continue;
				}
			}

			m_AssetDependencyGraph.ApplyStatus(event.m_Status, changes);
			for (const AssetDependencyChange& change : changes)
			{
				if (change.m_Model.m_Key.m_Kind != AssetKind::Model)
				{
					continue;
				}
				const ModelID modelId{ static_cast<uint32_t>(change.m_Model.m_Key.m_StableId) };
				Model* model = EditModel(modelId);
				if (!model || model->m_ContentGeneration != change.m_Model.m_ContentGeneration)
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

			if (event.m_Operation)
			{
				const bool completesOperation =
					event.m_OperationPhase == AssetStateEventOperationPhase::Completes;
				if (completesOperation)
				{
					// Keep the token current until its terminal state has been applied;
					// clearing it in the producer would make this event appear stale.
					if (textureOperation)
					{
						m_TextureAssets->CompleteResidencyOperation(*event.m_Operation);
					}
					else
					{
						AssetResidencyController::CompleteResidencyOperation(
							*operationLifecycle, *event.m_Operation);
					}
				}
				m_AssetResidencyController.RecordAcceptedStateEvent(completesOperation);
			}
		}
	}

	void AssetManager::BeginShutdown() noexcept
	{
		if (!m_AcceptingCommands)
		{
			return;
		}
		m_AcceptingCommands = false;

		AssetResidencyConfig shutdownConfig = m_AssetResidencyController.GetConfig();
		shutdownConfig.m_EnableAutomaticEviction = false;
		m_AssetResidencyController.SetConfig(shutdownConfig);
	}

	void AssetManager::PrepareForShutdown(const RHIFencePoint& lastSubmittedFence) noexcept
	{
		if (m_IsPreparedForShutdown)
		{
			return;
		}
		BeginShutdown();

		// Scheduler finalization may have emitted terminal residency events. Apply
		// them before classifying pending evictions as current or stale.
		DrainStateEvents();

		const size_t pendingEvictionCount = m_PendingResidencyEvictions.size();
		uint32_t cancelledCurrentEvictionCount = 0;
		for (const PendingResidencyEviction& eviction : m_PendingResidencyEvictions)
		{
			const AssetResidencyOperation operation = eviction.m_Operation;
			const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
			AssetLifecycle* lifecycle = contentVersion.m_Key.m_Kind == AssetKind::Texture
				? nullptr
				: FindResidencyLifecycle(contentVersion.m_Key);
			const bool currentOperation =
				contentVersion.m_Key.m_Kind == AssetKind::Texture
				? m_TextureAssets->IsCurrentResidencyOperation(operation)
				: lifecycle &&
				AssetResidencyController::IsCurrentOperation(*lifecycle, operation);
			if (!currentOperation)
			{
				m_AssetResidencyController.RecordStaleCompletion();
				m_AssetResidencyController.RecordEviction(true, eviction.m_ResidentBytes);
				continue;
			}

			bool queuedCompletion = false;
			if (contentVersion.m_Key.m_Kind == AssetKind::Texture)
			{
				queuedCompletion = m_TextureAssets->RestoreEvictionForShutdown(operation);
			}
			else if (contentVersion.m_Key.m_Kind == AssetKind::Mesh)
			{
				Mesh* mesh =
					EditMesh(MeshID{ static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
				if (mesh && mesh->m_ContentGeneration == contentVersion.m_ContentGeneration)
				{
					SetMeshState(*mesh,
						mesh->m_State == AssetState::Evicting ? AssetState::Ready : mesh->m_State,
						operation, AssetStateEventOperationPhase::Completes);
					queuedCompletion = true;
				}
			}

			GGLAB_ASSERT_MSG(queuedCompletion,
				"Pending residency eviction could not queue its shutdown completion.");
			if (!queuedCompletion)
			{
				if (contentVersion.m_Key.m_Kind == AssetKind::Texture)
				{
					m_TextureAssets->CompleteResidencyOperation(operation);
				}
				else if (lifecycle)
				{
					AssetResidencyController::CompleteResidencyOperation(*lifecycle, operation);
				}
			}
			++cancelledCurrentEvictionCount;
			m_AssetResidencyController.RecordEviction(true, eviction.m_ResidentBytes);
		}
		m_PendingResidencyEvictions.clear();
		m_PendingRuntimeRetirements.clear();

		// Completion events retire the operation serial only after their restored
		// state has passed the same stale-token validation as normal frame events.
		DrainStateEvents();

		if (pendingEvictionCount != 0)
		{
			GGLAB_LOG_GRAPHICS_INFO(
				"AssetManager shutdown resolved {} pending residency evictions ({} current, {} stale).",
				pendingEvictionCount, cancelledCurrentEvictionCount,
				pendingEvictionCount - cancelledCurrentEvictionCount);
		}
		GGLAB_ASSERT(m_PendingResidencyEvictions.empty());
		GGLAB_ASSERT_MSG(!m_AssetInterestTracker.HasOwners() &&
			!m_AssetInterestTracker.HasLeases() &&
			!m_AssetInterestTracker.HasInterests() &&
			!m_AssetInterestTracker.HasPublicationRetains(),
			"AssetManager shutdown requires all external asset ownership to be released.");
		GGLAB_ASSERT_MSG(m_ModelDependencyOwners.empty() && m_ModelDependencyLeaseTokens.empty(),
			"AssetManager shutdown requires model dependency ownership to be released.");
		GGLAB_ASSERT_MSG(!m_AssetLoadCoordinator.HasActiveOperations() &&
			!m_AssetLoadCoordinator.HasPendingCompletions(),
			"AssetManager shutdown requires asset load work to be drained.");
		GGLAB_ASSERT_MSG(!m_AssetStateEventQueue.HasPendingEvents(),
			"AssetManager shutdown requires asset state events to be drained.");

		const AssetUploadStatistics uploadStatistics = m_AssetUploadScheduler->GetStatistics();
		GGLAB_ASSERT_MSG(uploadStatistics.m_CpuPayloadQueue.m_PendingCount == 0 &&
			uploadStatistics.m_ResourcePublicationQueue.m_PendingCount == 0 &&
			uploadStatistics.m_UploadRecordingQueue.m_PendingCount == 0 &&
			uploadStatistics.m_GpuFinalizeQueue.m_PendingCount == 0 &&
			uploadStatistics.m_PendingCount == 0,
			"AssetManager shutdown requires the upload scheduler to be finalized.");

		// AssetManager owns texture records and performs their terminal, fence-aware
		// GPU release while the device and descriptor allocator are still alive.
		m_TextureAssets->Finalize(lastSubmittedFence);
		m_IsPreparedForShutdown = true;
		GGLAB_LOG_GRAPHICS_INFO(
			"AssetManager terminal shutdown prepared; texture GPU resources released at fence {}.",
			lastSubmittedFence.m_Value);
	}

	void AssetManager::SetResidencyConfig(const AssetResidencyConfig& config) noexcept
	{
		m_AssetResidencyController.SetConfig(config);
	}

	AssetResidencyStatistics AssetManager::GetResidencyStatistics() const noexcept
	{
		uint64_t pendingEvictionBytes = 0;
		for (const PendingResidencyEviction& eviction : m_PendingResidencyEvictions)
		{
			pendingEvictionBytes += eviction.m_ResidentBytes;
		}

		uint32_t reloadingAssetCount = 0;
		for (const auto& mesh : m_MeshStore.Entries() | std::views::values)
		{
			reloadingAssetCount += mesh->m_IsReloading ? 1u : 0u;
		}
		reloadingAssetCount += m_TextureAssets->GetReloadingTextureCount();
		return m_AssetResidencyController.GetStatistics(m_LogicalResidentBytes,
			pendingEvictionBytes, static_cast<uint32_t>(m_PendingResidencyEvictions.size()),
			reloadingAssetCount);
	}

	AssetOwnerId AssetManager::RegisterAssetOwner() noexcept
	{
		return m_AssetInterestTracker.RegisterOwner();
	}

	void AssetManager::UnregisterAssetOwner(AssetOwnerId owner) noexcept
	{
		m_AssetInterestTracker.UnregisterOwner(owner);
	}

	AssetLease AssetManager::AcquireAssetLease(AssetOwnerId owner, AssetKind kind,
		uint64_t stableId, uint64_t generation, TaskPriority priority) noexcept
	{
		if (!owner.IsValid() || !IsInterestAssetKind(kind) || priority == TaskPriority::Count)
		{
			return {};
		}
		const AssetLeaseAcquireResult result = m_AssetInterestTracker.AcquireLease(
			owner, MakeAssetContentVersion(kind, stableId, generation), priority);
		if (!result.IsValid())
		{
			return {};
		}
		CancelRuntimeRetirement(result.m_Change.m_ContentVersion);
		if (kind == AssetKind::Texture)
		{
			m_TextureAssets->ReviveTextureInterest(
				TextureID{ static_cast<uint32_t>(stableId) }, generation);
			GGLAB_UNUSED(RequestTextureResidency(
				TextureID{ static_cast<uint32_t>(stableId) }, generation, priority));
		}
		else if (kind == AssetKind::Mesh)
		{
			RequestMeshResidency(MeshID{ static_cast<uint32_t>(stableId) }, generation, priority);
		}
		HandleInterestChange(result.m_Change);
		if (kind == AssetKind::Model)
		{
			RequestModelResidency(ModelID{ static_cast<uint32_t>(stableId) }, generation);
		}
		return AssetLease(this, result.m_LeaseToken);
	}

	AssetPublicationRetain AssetManager::AcquirePublicationRetain(
		AssetKind kind, uint64_t stableId, uint64_t generation) noexcept
	{
		if (!IsInterestAssetKind(kind) || !m_AssetInterestTracker.AcquirePublicationRetain(
			MakeAssetContentVersion(kind, stableId, generation)))
		{
			return {};
		}
		CancelRuntimeRetirement(MakeAssetContentVersion(kind, stableId, generation));
		if (kind == AssetKind::Texture)
		{
			m_TextureAssets->ReviveTextureInterest(
				TextureID{ static_cast<uint32_t>(stableId) }, generation);
		}
		return AssetPublicationRetain(this, kind, stableId, generation);
	}

	void AssetManager::ReleasePublicationRetain(
		AssetKind kind, uint64_t stableId, uint64_t generation) noexcept
	{
		m_AssetInterestTracker.ReleasePublicationRetain(
			MakeAssetContentVersion(kind, stableId, generation));
		const AssetKey key{
			.m_Kind = kind,
			.m_StableId = stableId,
		};
		if (!HasActiveInterest(key) && !HasPublicationRetain(key, generation))
		{
			CancelAssetIfUnreferenced(key, generation);
		}
	}

	bool AssetManager::HasPublicationRetain(AssetKey key, uint64_t generation) const noexcept
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
			if (!HasPublicationRetain(
				change->m_ContentVersion.m_Key, change->m_ContentVersion.m_ContentGeneration))
			{
				QueueRuntimeRetirement(change->m_ContentVersion);
			}
			ReleaseModelDependencyInterests(
				ModelID{ static_cast<uint32_t>(change->m_ContentVersion.m_Key.m_StableId) });
		}
		CancelAssetIfUnreferenced(
			change->m_ContentVersion.m_Key, change->m_ContentVersion.m_ContentGeneration);
	}

	void AssetManager::UpdateAssetLeasePriority(uint64_t leaseToken, TaskPriority priority) noexcept
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
		if (!change.m_IsActive || (change.m_WasActive && !change.EffectivePriorityChanged()))
		{
			return;
		}
		ApplyInterestPriority(change.m_ContentVersion.m_Key,
			change.m_ContentVersion.m_ContentGeneration, change.m_EffectivePriority);
		if (change.EffectivePriorityChanged() &&
			change.m_ContentVersion.m_Key.m_Kind == AssetKind::Model)
		{
			UpdateModelDependencyPriorities(
				ModelID{ static_cast<uint32_t>(change.m_ContentVersion.m_Key.m_StableId) },
				change.m_EffectivePriority);
		}
	}

	void AssetManager::ApplyInterestPriority(
		AssetKey key, uint64_t generation, TaskPriority priority) noexcept
	{
		if (key.m_Kind == AssetKind::Model)
		{
			const ModelID modelId{ static_cast<uint32_t>(key.m_StableId) };
			GGLAB_UNUSED(m_AssetLoadCoordinator.UpdateModelImportPriority(
				MakeAssetContentVersion(key, generation), priority));
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(modelId, generation), priority));
		}
		else if (key.m_Kind == AssetKind::Texture)
		{
			m_TextureAssets->UpdateTextureLoadPriority(
				TextureID{ static_cast<uint32_t>(key.m_StableId) }, generation, priority);
		}
		else
		{
			const MeshID meshId{ static_cast<uint32_t>(key.m_StableId) };
			if (const Mesh* mesh = GetMesh(meshId); mesh && mesh->m_SourceModelId.IsValid())
			{
				if (const Model* sourceModel = GetModel(mesh->m_SourceModelId))
				{
					GGLAB_UNUSED(m_AssetLoadCoordinator.UpdateMeshReloadPriority(
						MakeAssetContentVersion(
							mesh->m_SourceModelId, sourceModel->m_ContentGeneration),
						priority));
				}
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->UpdateWorkPriority(
				MakeAssetContentVersion(meshId, generation), priority));
		}
	}

	TaskPriority AssetManager::GetEffectivePriority(
		AssetKey key, TaskPriority fallback) const noexcept
	{
		return m_AssetInterestTracker.GetEffectivePriority(key, fallback);
	}

	bool AssetManager::HasActiveInterest(AssetKey key) const noexcept
	{
		return m_AssetInterestTracker.HasActiveInterest(key);
	}

	bool AssetManager::HasPinnedDependentModel(
		AssetKind kind, uint64_t stableId, uint64_t generation) const noexcept
	{
		const std::span<const AssetContentVersion> dependents =
			m_AssetDependencyGraph.FindDependents(
				MakeAssetContentVersion(kind, stableId, generation));
		return std::ranges::any_of(dependents,
			[this](AssetContentVersion dependent) noexcept
			{
				if (dependent.m_Key.m_Kind != AssetKind::Model)
				{
					return false;
				}
				const Model* model =
					GetModel(ModelID{ static_cast<uint32_t>(dependent.m_Key.m_StableId) });
				return model && model->m_ContentGeneration == dependent.m_ContentGeneration &&
					model->m_ResidencyPolicy == AssetResidencyPolicy::Pinned;
			});
	}

	void AssetManager::RefreshModelDependencyInterests(
		ModelID modelId, uint64_t generation) noexcept
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

		const AssetOwnerId owner = RegisterAssetOwner();
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
					owner, AssetKind::Mesh, meshId.Value(), mesh->m_ContentGeneration, priority));
			}
		}
		for (TextureID textureId : textureIds)
		{
			if (const Texture* texture = m_TextureAssets->GetTexture(textureId))
			{
				retainLeaseToken(AcquireAssetLease(owner, AssetKind::Texture, textureId.Value(),
					texture->m_ContentGeneration, priority));
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
		ModelID modelId, TaskPriority priority) noexcept
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

	void AssetManager::QueueRuntimeRetirement(AssetContentVersion contentVersion) noexcept
	{
		if (!contentVersion.IsValid())
		{
			return;
		}
		const AssetKey key = contentVersion.m_Key;
		const bool reserved =
			(key.m_Kind == AssetKind::Model &&
				IsReservedModelId(ModelID{ static_cast<uint32_t>(key.m_StableId) })) ||
			(key.m_Kind == AssetKind::Mesh &&
				IsReservedMeshId(MeshID{ static_cast<uint32_t>(key.m_StableId) })) ||
			(key.m_Kind == AssetKind::Texture &&
				IsReservedTextureId(TextureID{ static_cast<uint32_t>(key.m_StableId) }));
		if (reserved)
		{
			return;
		}

		const auto pending = std::ranges::find(m_PendingRuntimeRetirements, contentVersion,
			&PendingRuntimeRetirement::m_ContentVersion);
		if (pending != m_PendingRuntimeRetirements.end())
		{
			return;
		}
		m_PendingRuntimeRetirements.push_back({
			.m_ContentVersion = contentVersion,
			.m_QueuedFrame = m_AssetUsageFrame,
			});
		++m_RuntimeRetirementRequestCount;
	}

	void AssetManager::CancelRuntimeRetirement(AssetContentVersion contentVersion) noexcept
	{
		const size_t removed = std::erase_if(m_PendingRuntimeRetirements,
			[contentVersion](const PendingRuntimeRetirement& pending) noexcept
			{ return pending.m_ContentVersion == contentVersion; });
		m_RuntimeRetirementCancellationCount += removed;
	}

	bool AssetManager::RetireRuntimeEntry(AssetContentVersion contentVersion) noexcept
	{
		const AssetKey key = contentVersion.m_Key;
		if (key.m_Kind == AssetKind::Texture)
		{
			const TextureID textureId{ static_cast<uint32_t>(key.m_StableId) };
			const Texture* texture = m_TextureAssets->GetTexture(textureId);
			if (!texture || texture->m_ContentGeneration != contentVersion.m_ContentGeneration)
			{
				return false;
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(contentVersion));
			return m_TextureAssets->RemoveTexture(textureId);
		}
		if (key.m_Kind == AssetKind::Mesh)
		{
			const MeshID meshId{ static_cast<uint32_t>(key.m_StableId) };
			const Mesh* mesh = GetMesh(meshId);
			if (!mesh || mesh->m_ContentGeneration != contentVersion.m_ContentGeneration)
			{
				return false;
			}
			GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(contentVersion));
			return RemoveMesh(meshId);
		}
		if (key.m_Kind != AssetKind::Model)
		{
			return false;
		}

		const ModelID modelId{ static_cast<uint32_t>(key.m_StableId) };
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return false;
		}

		// Retirement owns the final teardown of a model's internal dependency
		// interests. Cancel any not-yet-executed publication work first so it
		// cannot publish resources after the Store entry has been removed.
		GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(contentVersion));
		model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != contentVersion.m_ContentGeneration)
		{
			return false;
		}

		std::unordered_set<MaterialID> materials;
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			if (instance.m_MaterialId.IsValid())
			{
				materials.insert(instance.m_MaterialId);
			}
		}
		ReleaseModelDependencyInterests(modelId);
		UnregisterModelDependencies(modelId, contentVersion.m_ContentGeneration);
		m_AssetLoadCoordinator.DiscardModelImport(key);
		m_PendingModels.erase(modelId);
		if (!m_ModelStore.Remove(modelId))
		{
			return false;
		}

		for (MaterialID materialId : materials)
		{
			const bool referenced = std::ranges::any_of(m_ModelStore.Entries() | std::views::values,
				[materialId](const std::unique_ptr<Model>& candidate) noexcept
				{
					return std::ranges::any_of(candidate->m_MeshInstance,
						[materialId](const ModelMesh& instance) noexcept
						{ return instance.m_MaterialId == materialId; });
				});
			if (!referenced)
			{
				GGLAB_UNUSED(RemoveMaterial(materialId));
			}
		}
		return true;
	}

	void AssetManager::FinalizeRuntimeRetirements() noexcept
	{
		const AssetResidencyConfig& config = m_AssetResidencyController.GetConfig();
		if (config.m_MaxRuntimeRetirementsPerFrame == 0)
		{
			return;
		}

		const auto queueIfUnreferenced = [this](AssetContentVersion contentVersion) noexcept
			{
				if (!HasActiveInterest(contentVersion.m_Key) &&
					!HasPublicationRetain(contentVersion.m_Key, contentVersion.m_ContentGeneration))
				{
					QueueRuntimeRetirement(contentVersion);
				}
			};
		for (const auto& [modelId, model] : m_ModelStore.Entries())
		{
			queueIfUnreferenced(MakeAssetContentVersion(modelId, model->m_ContentGeneration));
		}
		for (const auto& [meshId, mesh] : m_MeshStore.Entries())
		{
			queueIfUnreferenced(MakeAssetContentVersion(meshId, mesh->m_ContentGeneration));
		}
		for (TextureID textureId : m_TextureAssets->GetTextureIds())
		{
			if (const Texture* texture = m_TextureAssets->GetTexture(textureId))
			{
				queueIfUnreferenced(
					MakeAssetContentVersion(textureId, texture->m_ContentGeneration));
			}
		}

		std::ranges::stable_sort(m_PendingRuntimeRetirements,
			[](const PendingRuntimeRetirement& lhs, const PendingRuntimeRetirement& rhs) noexcept
			{
				const auto priority = [](AssetKind kind) noexcept
					{
						return kind == AssetKind::Model ? 0u : kind == AssetKind::Mesh ? 1u : 2u;
					};
				return priority(lhs.m_ContentVersion.m_Key.m_Kind) <
					priority(rhs.m_ContentVersion.m_Key.m_Kind);
			});

		uint32_t retiredCount = 0;
		uint32_t retiredModelCount = 0;
		uint32_t retiredMeshCount = 0;
		uint32_t retiredTextureCount = 0;
		for (size_t pendingIndex = 0; pendingIndex < m_PendingRuntimeRetirements.size() &&
			retiredCount < config.m_MaxRuntimeRetirementsPerFrame;)
		{
			const PendingRuntimeRetirement pending = m_PendingRuntimeRetirements[pendingIndex];
			const AssetContentVersion contentVersion = pending.m_ContentVersion;
			const AssetKey key = contentVersion.m_Key;
			if (HasActiveInterest(key) ||
				HasPublicationRetain(key, contentVersion.m_ContentGeneration))
			{
				++m_RuntimeRetirementCancellationCount;
				m_PendingRuntimeRetirements.erase(
					m_PendingRuntimeRetirements.begin() + pendingIndex);
				continue;
			}
			if (m_AssetUsageFrame < pending.m_QueuedFrame ||
				m_AssetUsageFrame - pending.m_QueuedFrame < config.m_RuntimeEntryRetentionFrames)
			{
				++pendingIndex;
				continue;
			}

			const AssetLifecycle* lifecycle = nullptr;
			if (key.m_Kind == AssetKind::Model)
			{
				lifecycle = GetModel(ModelID{ static_cast<uint32_t>(key.m_StableId) });
			}
			else if (key.m_Kind == AssetKind::Mesh)
			{
				lifecycle = GetMesh(MeshID{ static_cast<uint32_t>(key.m_StableId) });
			}
			else if (key.m_Kind == AssetKind::Texture)
			{
				lifecycle =
					m_TextureAssets->GetTexture(TextureID{ static_cast<uint32_t>(key.m_StableId) });
			}
			if (!lifecycle || lifecycle->m_ContentGeneration != contentVersion.m_ContentGeneration)
			{
				m_PendingRuntimeRetirements.erase(
					m_PendingRuntimeRetirements.begin() + pendingIndex);
				continue;
			}
			if (lifecycle->m_ResidencyPolicy == AssetResidencyPolicy::Pinned ||
				((key.m_Kind == AssetKind::Mesh || key.m_Kind == AssetKind::Texture) &&
					HasPinnedDependentModel(
						key.m_Kind, key.m_StableId, contentVersion.m_ContentGeneration)) ||
				(lifecycle->m_State != AssetState::Ready &&
					lifecycle->m_State != AssetState::CpuReady &&
					!IsTerminalAssetState(lifecycle->m_State)))
			{
				++pendingIndex;
				continue;
			}

			if (!RetireRuntimeEntry(contentVersion))
			{
				++pendingIndex;
				continue;
			}
			++retiredCount;
			retiredModelCount += key.m_Kind == AssetKind::Model ? 1u : 0u;
			retiredMeshCount += key.m_Kind == AssetKind::Mesh ? 1u : 0u;
			retiredTextureCount += key.m_Kind == AssetKind::Texture ? 1u : 0u;
			++m_RuntimeRetirementCount;
			m_PendingRuntimeRetirements.erase(m_PendingRuntimeRetirements.begin() + pendingIndex);
		}
		if (retiredCount != 0)
		{
			GGLAB_LOG_GRAPHICS_INFO(
				"Retired unreferenced runtime entries (models={}, meshes={}, textures={}, pending={}).",
				retiredModelCount, retiredMeshCount, retiredTextureCount,
				m_PendingRuntimeRetirements.size());
		}
	}

	void AssetManager::CancelAssetIfUnreferenced(AssetKey key, uint64_t generation) noexcept
	{
		if (HasPublicationRetain(key, generation))
		{
			++m_PublicationProtectedCancellationCount;
			return;
		}
		if (key.m_Kind == AssetKind::Model)
		{
			CancelModelIfUnreferenced(ModelID{ static_cast<uint32_t>(key.m_StableId) }, generation);
		}
		else if (key.m_Kind == AssetKind::Texture)
		{
			const TextureID textureId{ static_cast<uint32_t>(key.m_StableId) };
			const Texture* texture = m_TextureAssets->GetTexture(textureId);
			if (!texture || texture->m_ContentGeneration != generation)
			{
				return;
			}
			QueueRuntimeRetirement(MakeAssetContentVersion(textureId, generation));
			if (texture->m_State == AssetState::Ready || IsTerminalAssetState(texture->m_State))
			{
				return;
			}
			if (texture->m_State == AssetState::Queued ||
				texture->m_State == AssetState::LoadingCpu)
			{
				++m_CpuCancellationCount;
			}
			else if (texture->m_Gpu.m_Texture.IsValid())
			{
				++m_GpuDeferredCancellationCount;
			}
			else
			{
				++m_ReadyCancellationCount;
			}
			m_TextureAssets->CancelTextureIfUnreferenced(textureId, generation);
		}
		else
		{
			CancelMeshIfUnreferenced(MeshID{ static_cast<uint32_t>(key.m_StableId) }, generation);
		}
	}

	void AssetManager::CancelModelIfUnreferenced(ModelID modelId, uint64_t generation) noexcept
	{
		Model* model = EditModel(modelId);
		if (!model || model->m_ContentGeneration != generation ||
			IsTerminalAssetState(model->m_State))
		{
			if (model && model->m_ContentGeneration == generation)
			{
				QueueRuntimeRetirement(MakeAssetContentVersion(modelId, generation));
			}
			return;
		}
		QueueRuntimeRetirement(MakeAssetContentVersion(modelId, generation));
		if (model->m_State == AssetState::Ready)
		{
			return;
		}
		if (model->m_ContentState == AssetContentState::Ready && !model->m_MeshInstance.empty() &&
			m_AssetDependencyGraph.FindModel(MakeAssetContentVersion(modelId, generation)))
		{
			model->m_CancelRequested = false;
			m_PendingModels.insert(modelId);
			return;
		}
		model->m_CancelRequested = true;
		GGLAB_UNUSED(
			m_AssetLoadCoordinator.CancelModelImport(MakeAssetContentVersion(modelId, generation)));
		m_AssetLoadCoordinator.DiscardModelImport(MakeAssetKey(modelId));
		const uint32_t cancelledReadyWork =
			m_AssetUploadScheduler->CancelReadyWork(MakeAssetContentVersion(modelId, generation));
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
		ProgressReporter(model->m_LoadProgress)
			.Report(0.96f, "Model loading cancelled",
				std::format("Model {} has no active owners", modelId.Value()));
	}

	void AssetManager::CancelMeshIfUnreferenced(MeshID meshId, uint64_t generation) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation || IsTerminalAssetState(mesh->m_State))
		{
			if (mesh && mesh->m_ContentGeneration == generation)
			{
				QueueRuntimeRetirement(MakeAssetContentVersion(meshId, generation));
			}
			return;
		}
		QueueRuntimeRetirement(MakeAssetContentVersion(meshId, generation));
		if (mesh->m_State == AssetState::Ready)
		{
			return;
		}
		if (mesh->m_IsReloading && !mesh->m_VertexBuffer && !mesh->m_IndexBuffer)
		{
			mesh->m_CancelRequested = true;
			GGLAB_UNUSED(m_AssetUploadScheduler->CancelReadyWork(
				MakeAssetContentVersion(meshId, generation)));
			mesh->m_IsReloading = false;
			AssetResidencyController::InvalidateResidencyOperation(*mesh);
			SetMeshState(*mesh, AssetState::CpuReady);
			++m_ReadyCancellationCount;
			return;
		}
		mesh->m_CancelRequested = true;
		const uint32_t cancelledReadyWork =
			m_AssetUploadScheduler->CancelReadyWork(MakeAssetContentVersion(meshId, generation));
		if (mesh->m_VertexBuffer || mesh->m_IndexBuffer)
		{
			++m_GpuDeferredCancellationCount;
		}
		else
		{
			GGLAB_UNUSED(cancelledReadyWork);
			++m_ReadyCancellationCount;
		}
		SetMeshState(*mesh, mesh->m_VertexBuffer || mesh->m_IndexBuffer ? AssetState::GpuProcessing
			: AssetState::Cancelled);
		ProgressReporter(mesh->m_LoadProgress)
			.Report(0.96f,
				mesh->m_VertexBuffer ? "Mesh cancellation pending GPU completion"
				: "Mesh upload cancelled",
				std::format("Mesh {} has no active owners", meshId.Value()));
	}

	AssetManager::ModelLoadRequest AssetManager::LoadModelAsync(
		const std::filesystem::path& path, TaskPriority priority) noexcept
	{
		if (!m_AcceptingCommands)
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager rejected model loading after shutdown began.");
			return {};
		}
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

		const AssetLoadSubmission submission = m_AssetLoadCoordinator.SubmitModelImport({
			.m_ContentVersion = MakeAssetContentVersion(modelId, generation),
			.m_SourcePath = canonicalPath,
			.m_ImportSettings = m_MaterialTextureSampling,
			.m_Priority = priority,
			.m_Progress = model->m_LoadProgress,
			});
		if (!submission.IsValid())
		{
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress)
				.Report(0.05f, "Model import submission failed",
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
		const std::filesystem::path& path, TextureSemantic semantic, TaskPriority priority) noexcept
	{
		if (!m_AcceptingCommands)
		{
			GGLAB_LOG_GRAPHICS_WARN("AssetManager rejected texture loading after shutdown began.");
			return {};
		}
		const TextureAssetSystem::TextureLoadRequest systemRequest =
			m_TextureAssets->LoadTextureAsync(path, semantic, priority);
		TextureLoadRequest request{
			.m_TextureId = systemRequest.m_TextureId,
			.m_Generation = systemRequest.m_Generation,
			.m_Task = systemRequest.m_Task,
		};
		if (request.IsValid())
		{
			if (!request.m_Task.IsValid())
			{
				request.m_Task =
					RequestTextureResidency(request.m_TextureId, request.m_Generation, priority);
			}
		}
		return request;
	}

	void AssetManager::Tick() noexcept
	{
		GGLAB_ASSERT_MSG(m_AcceptingCommands, "AssetManager::Tick called after shutdown began.");
		if (!m_AcceptingCommands)
		{
			return;
		}

		// Owner-thread phase 1: route worker completions into publication/upload work.
		DrainLoadCompletions();
		// Owner-thread phase 2: apply dependency events from previous work.
		DrainStateEvents();
		// Owner-thread phase 3: snapshot, plan, revalidate, and apply residency commands.
		++m_AssetUsageFrame;
		TickResidencyPhase();
		// Owner-thread phase 4: apply dependency events emitted by residency work.
		// Events emitted while this batch runs remain deferred to the next tick.
		DrainStateEvents();
		// Owner-thread phase 5: project dependency outcomes onto facade-visible models.
		std::erase_if(m_PendingModels,
			[this](ModelID modelId) noexcept { return RefreshModelState(modelId); });
		m_AssetResidencyController.EndFrame();
	}

	void AssetManager::TickResidencyPhase() noexcept
	{
		FinalizeResidencyEvictions();
		FinalizeRuntimeRetirements();
		const AssetResidencyInventorySnapshot inventory = BuildResidencyInventorySnapshot();
		m_LogicalResidentBytes = inventory.m_LogicalResidentBytes;
		AssetResidencyPlan plan = m_AssetResidencyController.BuildPlan(inventory);
		m_AssetResidencyController.RecordPlan(plan);
		ApplyResidencyPlan(plan);
	}

	AssetResidencyInventorySnapshot AssetManager::BuildResidencyInventorySnapshot() const noexcept
	{
		AssetResidencyInventorySnapshot snapshot{
			.m_Frame = m_AssetUsageFrame,
		};
		snapshot.m_Entries.reserve(
			m_MeshStore.Entries().size() + m_TextureAssets->GetTextureCount());
		const auto appendEntry = [this, &snapshot](AssetKey key) noexcept
			{
				AssetResidencyInventoryEntry entry;
				const bool built = BuildResidencyInventoryEntry(key, entry);
				GGLAB_ASSERT(built);
				if (!built)
				{
					return;
				}
				if (entry.m_Stamp.m_ResidencyState == AssetResidencyState::Resident ||
					entry.m_Stamp.m_ResidencyState == AssetResidencyState::Evicting)
				{
					snapshot.m_LogicalResidentBytes += entry.m_EstimatedBytes;
				}
				snapshot.m_Entries.push_back(std::move(entry));
			};
		for (const MeshID meshId : m_MeshStore.Entries() | std::views::keys)
		{
			appendEntry(MakeAssetKey(meshId));
		}
		for (const TextureID textureId : m_TextureAssets->GetTextureIds())
		{
			appendEntry(MakeAssetKey(textureId));
		}
		return snapshot;
	}

	bool AssetManager::BuildResidencyInventoryEntry(
		AssetKey key, AssetResidencyInventoryEntry& entry) const noexcept
	{
		if (key.m_Kind == AssetKind::Mesh)
		{
			const MeshID meshId{ static_cast<uint32_t>(key.m_StableId) };
			const Mesh* mesh = GetMesh(meshId);
			if (!mesh)
			{
				return false;
			}
			entry = {
				.m_Stamp =
					{
						.m_ContentVersion =
							MakeAssetContentVersion(meshId, mesh->m_ContentGeneration),
						.m_ResidencyEpoch = mesh->m_ResidencyEpoch,
						.m_ResidencyOperationSerial = mesh->m_ResidencyOperationSerial,
						.m_LastUsedFrame = mesh->m_LastUsedFrame,
						.m_State = mesh->m_State,
						.m_ContentState = mesh->m_ContentState,
						.m_ResidencyState = mesh->m_ResidencyState,
						.m_ResidencyPolicy = mesh->m_ResidencyPolicy,
					},
				.m_EstimatedBytes = EstimateMeshResidentBytes(*mesh),
				.m_IsReserved = IsReservedMeshId(meshId),
				.m_HasReloadSource =
					mesh->m_SourceModelId.IsValid() &&
					mesh->m_SourceMeshIndex != std::numeric_limits<uint32_t>::max(),
				.m_HasActiveInterest = HasActiveInterest(key),
				.m_HasPublicationRetain = HasPublicationRetain(key, mesh->m_ContentGeneration),
				.m_HasPinnedDependentModel = HasPinnedDependentModel(
					AssetKind::Mesh, meshId.Value(), mesh->m_ContentGeneration),
			};
			return true;
		}
		if (key.m_Kind == AssetKind::Texture)
		{
			const TextureID textureId{ static_cast<uint32_t>(key.m_StableId) };
			const Texture* texture = m_TextureAssets->GetTexture(textureId);
			if (!texture)
			{
				return false;
			}
			entry = {
				.m_Stamp =
					{
						.m_ContentVersion =
							MakeAssetContentVersion(textureId, texture->m_ContentGeneration),
						.m_ResidencyEpoch = texture->m_ResidencyEpoch,
						.m_ResidencyOperationSerial = texture->m_ResidencyOperationSerial,
						.m_LastUsedFrame = texture->m_LastUsedFrame,
						.m_State = texture->m_State,
						.m_ContentState = texture->m_ContentState,
						.m_ResidencyState = texture->m_ResidencyState,
						.m_ResidencyPolicy = texture->m_ResidencyPolicy,
					},
				.m_EstimatedBytes = EstimateTextureResidentBytes(*texture),
				.m_IsReserved = IsReservedTextureId(textureId),
				.m_HasReloadSource = !texture->m_Source.m_CanonicalPath.empty(),
				.m_HasActiveInterest = HasActiveInterest(key),
				.m_HasPublicationRetain = HasPublicationRetain(key, texture->m_ContentGeneration),
				.m_HasPinnedDependentModel = HasPinnedDependentModel(
					AssetKind::Texture, textureId.Value(), texture->m_ContentGeneration),
			};
			return true;
		}
		return false;
	}

	AssetLifecycle* AssetManager::FindResidencyLifecycle(AssetKey key) noexcept
	{
		return key.m_Kind == AssetKind::Mesh
			? EditMesh(MeshID{ static_cast<uint32_t>(key.m_StableId) })
			: nullptr;
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

			const AssetResidencyOperation operation = eviction.m_Operation;
			const AssetContentVersion contentVersion = operation.m_Token.m_ContentVersion;
			const AssetKey key = contentVersion.m_Key;
			const bool protectedByInterest =
				HasActiveInterest(key) ||
				HasPublicationRetain(key, contentVersion.m_ContentGeneration);
			bool finalized = false;
			bool cancelled = protectedByInterest;
			AssetLifecycle* lifecycle =
				key.m_Kind == AssetKind::Texture ? nullptr : FindResidencyLifecycle(key);
			const bool currentOperation =
				key.m_Kind == AssetKind::Texture
				? m_TextureAssets->IsCurrentResidencyOperation(operation)
				: lifecycle &&
				AssetResidencyController::IsCurrentOperation(*lifecycle, operation);
			if (!currentOperation)
			{
				m_AssetResidencyController.RecordStaleCompletion();
				cancelled = true;
				finalized = true;
			}
			else if (key.m_Kind == AssetKind::Texture)
			{
				const TextureAssetSystem::EvictionFinalizationResult result =
					m_TextureAssets->FinalizeEviction(operation, protectedByInterest);
				finalized = result.m_Finalized;
				cancelled = result.m_Cancelled;
			}
			else if (key.m_Kind == AssetKind::Mesh)
			{
				Mesh* mesh = EditMesh(MeshID{ static_cast<uint32_t>(key.m_StableId) });
				if (!mesh || mesh->m_ContentGeneration != contentVersion.m_ContentGeneration)
				{
					finalized = true;
				}
				else if (mesh->m_State != AssetState::Evicting)
				{
					cancelled = mesh->m_State == AssetState::Ready;
					AssetResidencyController::CompleteResidencyOperation(*mesh, operation);
					finalized = true;
				}
				else if (protectedByInterest ||
					mesh->m_ResidencyPolicy == AssetResidencyPolicy::Pinned)
				{
					SetMeshState(*mesh, AssetState::Ready, operation,
						AssetStateEventOperationPhase::Completes);
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
					SetMeshState(*mesh, AssetState::CpuReady, operation,
						AssetStateEventOperationPhase::Completes);
					ProgressReporter(mesh->m_LoadProgress)
						.Report(1.0f, "Mesh GPU residency released",
							std::format("Mesh {}", mesh->m_Id.Value()));
					finalized = true;
				}
			}

			if (!finalized)
			{
				++iterator;
				continue;
			}
			m_AssetResidencyController.RecordEviction(cancelled, eviction.m_ResidentBytes);
			iterator = m_PendingResidencyEvictions.erase(iterator);
		}
	}

	bool AssetManager::ApplyResidencyAction(
		const AssetResidencyAction& action, uint64_t projectedResidentBytes) noexcept
	{
		const AssetContentVersion contentVersion = action.m_ExpectedStamp.m_ContentVersion;
		AssetLifecycle* lifecycle = contentVersion.m_Key.m_Kind == AssetKind::Texture
			? nullptr
			: FindResidencyLifecycle(contentVersion.m_Key);
		const bool assetExists =
			contentVersion.m_Key.m_Kind == AssetKind::Texture
			? m_TextureAssets->GetTexture(
				TextureID{ static_cast<uint32_t>(contentVersion.m_Key.m_StableId) }) != nullptr
			: lifecycle != nullptr;
		AssetResidencyInventoryEntry currentEntry;
		if (!assetExists || !BuildResidencyInventoryEntry(contentVersion.m_Key, currentEntry) ||
			!m_AssetResidencyController.StillEligible(
				action, currentEntry, m_AssetUsageFrame, projectedResidentBytes))
		{
			m_AssetResidencyController.RecordRevalidationRejection();
			return false;
		}

		const AssetResidencyOperation operation =
			contentVersion.m_Key.m_Kind == AssetKind::Texture
			? m_TextureAssets->BeginResidencyOperation(contentVersion,
				AssetResidencyOperationKind::Evict, m_AssetResidencyController)
			: m_AssetResidencyController.BeginResidencyOperation(
				*lifecycle, contentVersion, AssetResidencyOperationKind::Evict);
		if (!operation.IsValid())
		{
			m_AssetResidencyController.RecordRevalidationRejection();
			return false;
		}

		if (contentVersion.m_Key.m_Kind == AssetKind::Texture)
		{
			// TextureAssetSystem began the operation and applied Evicting atomically.
		}
		else if (contentVersion.m_Key.m_Kind == AssetKind::Mesh)
		{
			Mesh* mesh = EditMesh(MeshID{ static_cast<uint32_t>(contentVersion.m_Key.m_StableId) });
			GGLAB_ASSERT_NOT_NULL(mesh);
			SetMeshState(
				*mesh, AssetState::Evicting, operation, AssetStateEventOperationPhase::InProgress);
		}
		else
		{
			AssetResidencyController::CompleteResidencyOperation(*lifecycle, operation);
			return false;
		}

		m_PendingResidencyEvictions.push_back({
			.m_Operation = operation,
			.m_ResidentBytes = action.m_EstimatedBytes,
			.m_QuiescedFrame = m_AssetUsageFrame,
			});
		return true;
	}

	void AssetManager::ApplyResidencyPlan(const AssetResidencyPlan& plan) noexcept
	{
		if (plan.m_SnapshotFrame != m_AssetUsageFrame)
		{
			return;
		}
		uint64_t projectedResidentBytes = plan.m_LogicalResidentBytes;
		for (const AssetResidencyAction& action : plan.m_Actions)
		{
			if (ApplyResidencyAction(action, projectedResidentBytes))
			{
				projectedResidentBytes = projectedResidentBytes > action.m_EstimatedBytes
					? projectedResidentBytes - action.m_EstimatedBytes
					: 0;
			}
		}
	}

	void AssetManager::RequestModelResidency(ModelID modelId, uint64_t generation) noexcept
	{
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation || model->m_MeshInstance.empty())
		{
			return;
		}
		RefreshModelDependencyInterests(modelId, generation);
		if (m_AssetDependencyGraph.FindModel(MakeAssetContentVersion(modelId, generation)))
		{
			m_PendingModels.insert(modelId);
		}
	}

	TaskHandle AssetManager::RequestTextureResidency(
		TextureID textureId, uint64_t generation, TaskPriority priority) noexcept
	{
		return m_TextureAssets->RequestResidency(
			textureId, generation, priority, m_AssetResidencyController);
	}

	void AssetManager::RequestMeshResidency(
		MeshID meshId, uint64_t generation, TaskPriority priority) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation)
		{
			return;
		}
		if (mesh->m_State == AssetState::Evicting)
		{
			m_AssetResidencyController.RecordReloadRequest(false);
			const AssetResidencyOperation operation =
				m_AssetResidencyController.BeginResidencyOperation(*mesh,
					MakeAssetContentVersion(meshId, generation),
					AssetResidencyOperationKind::Reload);
			SetMeshState(
				*mesh, AssetState::Ready, operation, AssetStateEventOperationPhase::Completes);
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

		m_AssetResidencyController.RecordReloadRequest(mesh->m_IsReloading);
		if (mesh->m_IsReloading)
		{
			return;
		}
		mesh->m_CancelRequested = false;
		mesh->m_IsReloading = true;
		const AssetResidencyOperation operation =
			m_AssetResidencyController.BeginResidencyOperation(*mesh,
				MakeAssetContentVersion(meshId, generation), AssetResidencyOperationKind::Reload);
		if (!operation.IsValid())
		{
			mesh->m_IsReloading = false;
			return;
		}
		if (m_AssetLoadCoordinator.HasMeshReload(
			MakeAssetContentVersion(mesh->m_SourceModelId, sourceModel->m_ContentGeneration)))
		{
			m_AssetResidencyController.RecordReloadCoalesced();
			return;
		}
		QueueMeshResidencyReload(mesh->m_SourceModelId, priority);
	}

	void AssetManager::QueueMeshResidencyReload(
		ModelID sourceModelId, TaskPriority priority) noexcept
	{
		const Model* sourceModel = GetModel(sourceModelId);
		if (!sourceModel || sourceModel->m_SourcePath.empty())
		{
			return;
		}
		const std::filesystem::path sourcePath = sourceModel->m_SourcePath;
		const uint64_t sourceGeneration = sourceModel->m_ContentGeneration;
		const AssetLoadSubmission submission = m_AssetLoadCoordinator.SubmitMeshReload({
			.m_SourceModelVersion = MakeAssetContentVersion(sourceModelId, sourceGeneration),
			.m_SourcePath = sourcePath,
			.m_ImportSettings = m_MaterialTextureSampling,
			.m_Priority = priority,
			.m_ExpectedArtifactContentDigest = sourceModel->m_ImportArtifactContentDigest,
			});
		if (!submission.IsValid())
		{
			for (const auto& [meshId, mesh] : m_MeshStore.Entries())
			{
				if (mesh->m_SourceModelId == sourceModelId)
				{
					const AssetResidencyOperation operation{
						.m_Token = MakeAssetOperationToken(
							MakeAssetContentVersion(meshId, mesh->m_ContentGeneration),
							mesh->m_ResidencyOperationSerial),
						.m_Kind = AssetResidencyOperationKind::Reload,
					};
					mesh->m_IsReloading = false;
					SetMeshState(*mesh, AssetState::CpuReady, operation,
						AssetStateEventOperationPhase::Completes);
				}
			}
			return;
		}
	}

	void AssetManager::MarkModelUsed(ModelID modelId) noexcept
	{
		if (Model* model = EditModel(modelId))
		{
			AssetResidencyController::MarkAssetUsed(*model, m_AssetUsageFrame);
		}
	}

	void AssetManager::MarkMeshUsed(MeshID meshId) noexcept
	{
		if (Mesh* mesh = EditMesh(meshId))
		{
			AssetResidencyController::MarkAssetUsed(*mesh, m_AssetUsageFrame);
		}
	}

	void AssetManager::MarkTextureUsed(TextureID textureId) noexcept
	{
		m_TextureAssets->MarkUsed(textureId, m_AssetUsageFrame);
	}

	void AssetManager::SetMeshState(Mesh& mesh, AssetState state,
		AssetResidencyOperation residencyOperation,
		AssetStateEventOperationPhase operationPhase) noexcept
	{
		const bool hasOperation = residencyOperation.IsValid();
		GGLAB_ASSERT_MSG(hasOperation == (operationPhase != AssetStateEventOperationPhase::None),
			"Mesh state event operation phase does not match its operation token.");
		const AssetState previousState = mesh.m_State;
		SetAssetState(mesh, state);
		// A same-state terminal transition still has to retire its operation.
		if (previousState != state || operationPhase == AssetStateEventOperationPhase::Completes)
		{
			QueueDependencyStateChange(MakeAssetContentVersion(mesh.m_Id, mesh.m_ContentGeneration),
				mesh.m_ContentState, mesh.m_ResidencyState,
				hasOperation ? std::optional{ residencyOperation.m_Token } : std::nullopt,
				operationPhase);
		}
	}

	void AssetManager::RegisterModelDependencies(ModelID modelId, uint64_t generation) noexcept
	{
		const Model* model = GetModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}

		std::vector<DependencyStatus> dependencies;
		uint32_t structuralFailureCount = 0;
		const auto addDependency = [&dependencies](AssetContentVersion contentVersion,
			const AssetLifecycle& lifecycle) noexcept
			{ dependencies.push_back(MakeDependencyStatus(contentVersion, lifecycle)); };

		if (model->m_MeshInstance.empty())
		{
			++structuralFailureCount;
		}
		for (const ModelMesh& instance : model->m_MeshInstance)
		{
			if (const Mesh* mesh = GetMesh(instance.m_MeshId))
			{
				addDependency(
					MakeAssetContentVersion(instance.m_MeshId, mesh->m_ContentGeneration), *mesh);
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
				if (const Texture* texture = m_TextureAssets->GetTexture(textureId))
				{
					addDependency(
						MakeAssetContentVersion(textureId, texture->m_ContentGeneration), *texture);
				}
				else
				{
					++structuralFailureCount;
				}
			}
		}

		GGLAB_UNUSED(m_AssetDependencyGraph.RegisterModel(
			MakeAssetContentVersion(modelId, generation), dependencies, structuralFailureCount));
	}

	void AssetManager::UnregisterModelDependencies(ModelID modelId, uint64_t generation) noexcept
	{
		m_AssetDependencyGraph.UnregisterModel(MakeAssetContentVersion(modelId, generation));
	}

	void AssetManager::QueueDependencyStateChange(AssetContentVersion contentVersion,
		AssetContentState contentState, AssetResidencyState residencyState,
		std::optional<AssetOperationToken> operation,
		AssetStateEventOperationPhase operationPhase) noexcept
	{
		m_AssetStateEventQueue.Push(
			{
				.m_ContentVersion = contentVersion,
				.m_ContentState = contentState,
				.m_ResidencyState = residencyState,
			},
			operation, operationPhase);
	}

	bool AssetManager::SetModelResidencyPolicy(
		ModelID modelId, AssetResidencyPolicy policy) noexcept
	{
		Model* model = EditModel(modelId);
		if (!model || !AssetResidencyController::SetResidencyPolicy(
			*model, policy, IsReservedModelId(modelId)))
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
				RequestMeshResidency(meshId, mesh->m_ContentGeneration, TaskPriority::Normal);
			}
		}
		for (TextureID textureId : textureIds)
		{
			if (const Texture* texture = m_TextureAssets->GetTexture(textureId))
			{
				GGLAB_UNUSED(RequestTextureResidency(
					textureId, texture->m_ContentGeneration, TaskPriority::Normal));
			}
		}
		m_PendingModels.insert(modelId);
		return true;
	}

	bool AssetManager::SetMeshResidencyPolicy(MeshID meshId, AssetResidencyPolicy policy) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh ||
			!AssetResidencyController::SetResidencyPolicy(*mesh, policy, IsReservedMeshId(meshId)))
		{
			return false;
		}
		if (policy == AssetResidencyPolicy::Pinned)
		{
			RequestMeshResidency(meshId, mesh->m_ContentGeneration, TaskPriority::Normal);
		}
		return true;
	}

	bool AssetManager::SetTextureResidencyPolicy(
		TextureID textureId, AssetResidencyPolicy policy) noexcept
	{
		if (!m_TextureAssets->SetResidencyPolicy(textureId, policy))
		{
			return false;
		}
		if (policy == AssetResidencyPolicy::Pinned)
		{
			GGLAB_UNUSED(RequestTextureResidency(
				textureId, GetTextureContentRef(textureId).m_Generation, TaskPriority::Normal));
		}
		return true;
	}

	TextureContentRef AssetManager::GetTextureContentRef(TextureID textureId) const noexcept
	{
		return m_TextureAssets->GetTextureContentRef(textureId);
	}

	std::optional<AssetContentFingerprint> AssetManager::GetTextureContentFingerprint(
		TextureContentRef content) const noexcept
	{
		return m_TextureAssets->GetTextureContentFingerprint(content);
	}

	std::optional<AssetState> AssetManager::GetTextureState(
		TextureContentRef content) const noexcept
	{
		return m_TextureAssets->GetTextureState(content);
	}

	std::optional<ResidentTextureResource> AssetManager::GetResidentTextureResource(
		TextureContentRef content) const noexcept
	{
		return m_TextureAssets->GetResidentTextureResource(content);
	}

	TextureArtifactCacheStatistics AssetManager::GetTextureArtifactCacheStatistics() const noexcept
	{
		return m_TextureAssets->GetArtifactCacheStatistics();
	}

	void AssetManager::ClearTextureArtifactCache() noexcept
	{
		m_TextureAssets->ClearArtifactCache();
	}

	ModelImportArtifactCacheStatistics AssetManager::GetModelImportArtifactCacheStatistics()
		const noexcept
	{
		return m_ModelImportArtifactCache.GetStatistics();
	}

	void AssetManager::ClearModelImportArtifactCache() noexcept
	{
		m_ModelImportArtifactCache.Clear();
	}

	LocalDerivedDataStoreStatistics AssetManager::GetTextureDerivedDataStatistics() const noexcept
	{
		return m_AssetLoadCoordinator.GetTextureDerivedDataStatistics();
	}

	TextureDerivedDataCoordinatorStatistics AssetManager::
		GetTextureDerivedDataCoordinatorStatistics() const noexcept
	{
		return m_AssetLoadCoordinator.GetTextureDerivedDataCoordinatorStatistics();
	}

	bool AssetManager::ClearTextureDerivedDataCache() noexcept
	{
		return m_AssetLoadCoordinator.ClearTextureDerivedDataCache();
	}

	std::vector<TextureAssetReadInfo> AssetManager::GetTextureAssetReadInfos() const
	{
		return m_TextureAssets->GetTextureAssetReadInfos();
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

	void AssetManager::RollbackPublicationMesh(MeshID meshId, uint64_t generation) noexcept
	{
		Mesh* mesh = EditMesh(meshId);
		if (!mesh || mesh->m_ContentGeneration != generation)
		{
			return;
		}

		mesh->m_CancelRequested = true;
		GGLAB_UNUSED(
			m_AssetUploadScheduler->CancelReadyWork(MakeAssetContentVersion(meshId, generation)));
		if ((mesh->m_VertexBuffer || mesh->m_IndexBuffer) && mesh->m_State != AssetState::Ready)
		{
			m_PublicationOrphanedMeshes.insert(meshId);
			SetMeshState(*mesh, AssetState::GpuProcessing);
			ProgressReporter(mesh->m_LoadProgress)
				.Report(0.96f, "Mesh publication rollback pending GPU completion");
			return;
		}

		GGLAB_UNUSED(RemoveMesh(meshId));
	}

	MeshID AssetManager::AddProceduralMesh(
		std::unique_ptr<Mesh>&& mesh, MeshUploadData& meshUploadData) noexcept
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
			BeginAssetContentGeneration(*storedMesh, 1, AssetState::CpuReady,
				IsReservedMeshId(meshId) ? AssetResidencyPolicy::Pinned
				: AssetResidencyPolicy::Cacheable);
		}

		meshUploadData.m_MeshId = meshId;
		SetMeshState(*storedMesh, AssetState::CpuReady);
		ProgressReporter(storedMesh->m_LoadProgress)
			.Report(0.62f, "Procedural mesh CPU data ready",
				std::format("{} vertices, {} indices", meshUploadData.m_VerticesData.size(),
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

	MaterialID AssetManager::AddProceduralMaterial(std::unique_ptr<Material>&& material) noexcept
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
			BeginAssetContentGeneration(*storedModel, 1, AssetState::CpuReady,
				IsReservedModelId(modelId) ? AssetResidencyPolicy::Pinned
				: AssetResidencyPolicy::Cacheable);
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

	uint32_t AssetManager::ResolveSrvIndex(
		TextureID textureId, ReservedTextureIDIndex fallback) const noexcept
	{
		return m_TextureAssets->ResolveSrvIndex(textureId, fallback);
	}

	bool AssetManager::UploadMesh(MeshUploadData& uploadData, TransferBatch& transferBatch,
		AssetResidencyOperation residencyOperation) noexcept
	{
		auto* mesh = EditMesh(uploadData.m_MeshId);
		if (mesh == nullptr)
		{
			GGLAB_ASSERT_MSG(false, "UploadMesh: Invalid MeshID, check it!");
			return false;
		}
		const AssetStateEventOperationPhase operationPhase =
			residencyOperation.IsValid() ? AssetStateEventOperationPhase::InProgress
			: AssetStateEventOperationPhase::None;
		SetMeshState(*mesh, AssetState::UploadQueued, residencyOperation, operationPhase);

		const std::span<const Vertex> verticesData = uploadData.GetVertices();
		const std::span<const uint32_t> indicesData = uploadData.GetIndices();

		const auto vertexCount = static_cast<uint32_t>(verticesData.size());
		const auto indexCount = static_cast<uint32_t>(indicesData.size());
		ProgressReporter(mesh->m_LoadProgress)
			.Report(0.68f, "Recording mesh buffer upload",
				std::format("{} vertices, {} indices", vertexCount, indexCount));

		mesh->m_VertexCount = vertexCount;
		mesh->m_IndexCount = indexCount;

		const auto vertexBufferSize = static_cast<uint64_t>(vertexCount) * sizeof(Vertex);
		const auto indexBufferSize = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);

		if (vertexBufferSize == 0 || indexBufferSize == 0)
		{
			SetMeshState(*mesh, AssetState::Failed, residencyOperation, operationPhase);
			GGLAB_LOG_GRAPHICS_WARN("AssetManager::UploadMesh received an empty mesh.");
			return false;
		}
		if (vertexBufferSize > std::numeric_limits<uint32_t>::max() ||
			indexBufferSize > std::numeric_limits<uint32_t>::max())
		{
			SetMeshState(*mesh, AssetState::Failed, residencyOperation, operationPhase);
			GGLAB_LOG_GRAPHICS_ERROR(
				"AssetManager::UploadMesh mesh buffers exceed RHI binding size limits.");
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

		const std::string_view meshName =
			mesh->m_Name.Name().empty() ? std::string_view("UnnamedMesh") : mesh->m_Name.Name();
		const RHIResourceDebugIdentityDesc vertexDebugIdentity{
			.m_Domain = RHIResourceDebugDomain::Asset,
			.m_Category = "Mesh.VertexBuffer",
			.m_Label = meshName,
			.m_StableId = uploadData.m_MeshId.Value(),
		};
		const RHIResourceDebugIdentityDesc indexDebugIdentity{
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
			SetMeshState(*mesh, AssetState::Failed, residencyOperation, operationPhase);
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
		// TransferBatch copies the source into staging memory synchronously.
		// The immutable model artifact is no longer needed after both copies return.
		uploadData.ReleaseBorrowedSource();
		GGLAB_ASSERT_MSG(vertexUploadSucceeded && indexUploadSucceeded,
			"AssetManager failed to record mesh buffer uploads.");
		if (!vertexUploadSucceeded || !indexUploadSucceeded)
		{
			SetMeshState(*mesh, AssetState::Failed, residencyOperation, operationPhase);
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

		SetMeshState(*mesh, AssetState::GpuProcessing, residencyOperation, operationPhase);
		return true;
	}

	bool AssetManager::QueueMeshUpload(MeshUploadData&& uploadData, TaskPriority priority,
		AssetResidencyOperation residencyOperation) noexcept
	{
		Mesh* mesh = EditMesh(uploadData.m_MeshId);
		if (!mesh || uploadData.GetVertices().empty() || uploadData.GetIndices().empty())
		{
			return false;
		}
		if (residencyOperation.IsValid() &&
			(residencyOperation.m_Kind != AssetResidencyOperationKind::Reload ||
				!AssetResidencyController::IsCurrentOperation(*mesh, residencyOperation)))
		{
			m_AssetResidencyController.RecordStaleCompletion();
			return false;
		}

		const MeshID meshId = uploadData.m_MeshId;
		const uint64_t generation = mesh->m_ContentGeneration;
		const AssetStreamingWorkEstimate estimate = EstimateMeshUpload(uploadData);
		if (mesh->m_State != AssetState::Publishing)
		{
			SetMeshState(*mesh, AssetState::CpuReady, residencyOperation,
				residencyOperation.IsValid() ? AssetStateEventOperationPhase::InProgress
				: AssetStateEventOperationPhase::None);
		}
		ProgressReporter(mesh->m_LoadProgress)
			.Report(0.62f, "Waiting for mesh upload admission",
				std::format("{} bytes", estimate.m_StagingBytes));
		auto payload = std::make_shared<MeshUploadData>(std::move(uploadData));
		m_AssetUploadScheduler->EnqueueUploadRecording(
			{
				.m_Name = std::format("Mesh {}", meshId.Value()),
				.m_Identity =
					{
						.m_Kind = AssetStreamingWorkKind::Mesh,
						.m_StableId = meshId.Value(),
						.m_Generation = generation,
					},
				.m_Estimate = estimate,
				.m_Priority = priority,
				.m_Progress = mesh->m_LoadProgress,
			},
			[this, meshId, generation, estimate, priority, residencyOperation,
			payload]() mutable noexcept
			{
				Mesh* currentMesh = EditMesh(meshId);
				if (!currentMesh || currentMesh->m_ContentGeneration != generation)
				{
					return;
				}
				if (residencyOperation.IsValid() &&
					!AssetResidencyController::IsCurrentOperation(*currentMesh, residencyOperation))
				{
					m_AssetResidencyController.RecordStaleCompletion();
					return;
				}
				if (currentMesh->m_CancelRequested)
				{
					const bool residencyReload = currentMesh->m_IsReloading;
					currentMesh->m_IsReloading = false;
					SetMeshState(*currentMesh,
						residencyReload ? AssetState::CpuReady : AssetState::Cancelled,
						residencyOperation,
						residencyReload ? AssetStateEventOperationPhase::Completes
						: AssetStateEventOperationPhase::None);
					return;
				}

				GGLAB_UNUSED(m_AssetUploadScheduler->RecordUpload(
					{
						.m_Name = std::format("Mesh {}", meshId.Value()),
						.m_Identity =
							{
								.m_Kind = AssetStreamingWorkKind::Mesh,
								.m_StableId = meshId.Value(),
								.m_Generation = generation,
							},
						.m_Estimate = estimate,
						.m_Priority = priority,
						.m_Progress = currentMesh->m_LoadProgress,
					},
					[this, payload, residencyOperation](TransferBatch& batch) noexcept
					{ return UploadMesh(*payload, batch, residencyOperation); },
					[this, meshId, generation, residencyOperation](
						const AssetUploadCompletionInfo& completion) noexcept
					{
						const Mesh* currentMesh = GetMesh(meshId);
						if (!currentMesh || currentMesh->m_ContentGeneration != generation)
						{
							return;
						}
						CompleteMeshUpload(meshId,
							completion.m_Status == AssetUploadStatus::Succeeded,
							residencyOperation);
					}));
			});
		return true;
	}

	void AssetManager::CompleteMeshUpload(
		MeshID meshId, bool succeeded, AssetResidencyOperation residencyOperation) noexcept
	{
		auto* mesh = EditMesh(meshId);
		if (!mesh)
		{
			return;
		}
		if (residencyOperation.IsValid() &&
			!AssetResidencyController::IsCurrentOperation(*mesh, residencyOperation))
		{
			m_AssetResidencyController.RecordStaleCompletion();
			return;
		}
		const bool residencyReload = mesh->m_IsReloading;
		const bool publicationOrphan = m_PublicationOrphanedMeshes.contains(meshId);
		const bool cancelled = mesh->m_CancelRequested || publicationOrphan;
		const bool publishSucceeded = succeeded && !cancelled;
		SetMeshState(*mesh,
			cancelled ? (residencyReload ? AssetState::CpuReady : AssetState::Cancelled)
			: (publishSucceeded
				? AssetState::Ready
				: (residencyReload ? AssetState::CpuReady : AssetState::Failed)),
			residencyOperation,
			residencyReload ? AssetStateEventOperationPhase::Completes
			: AssetStateEventOperationPhase::None);
		ProgressReporter(mesh->m_LoadProgress)
			.Report(publishSucceeded ? 1.0f : 0.96f,
				publishSucceeded ? "Mesh ready"
				: (cancelled ? "Mesh upload cancelled" : "Mesh GPU upload failed"),
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

	void AssetManager::RouteModelImportCompletion(AssetOperationToken operation,
		const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentModelImport(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		const ModelID modelId{ static_cast<uint32_t>(operation.m_ContentVersion.m_Key.m_StableId) };
		const Model* model = GetModel(modelId);
		if (!model ||
			model->m_ContentGeneration != operation.m_ContentVersion.m_ContentGeneration ||
			model->m_CancelRequested)
		{
			m_AssetLoadCoordinator.CompleteModelImport(operation);
			return;
		}

		if (artifact)
		{
			artifact = m_ModelImportArtifactCache.Admit(std::move(artifact));
		}
		m_AssetUploadScheduler->EnqueueCpuPayload(
			{
				.m_Name = completion.m_Name,
				.m_Identity =
					{
						.m_Kind = AssetStreamingWorkKind::Model,
						.m_StableId = modelId.Value(),
						.m_Generation = operation.m_ContentVersion.m_ContentGeneration,
					},
				.m_Estimate =
					artifact ? EstimateImportedModel(*artifact) : AssetStreamingWorkEstimate{},
				.m_Priority = completion.m_Priority,
				.m_Progress = model->m_LoadProgress,
			},
			[this, operation, completion, artifact = std::move(artifact)]() mutable noexcept
			{ CompleteModelLoad(operation, completion, std::move(artifact)); });
	}

	void AssetManager::RouteMeshReloadCompletion(AssetOperationToken operation,
		const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentMeshReload(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		const ModelID sourceModelId{
			static_cast<uint32_t>(operation.m_ContentVersion.m_Key.m_StableId) };
		const Model* sourceModel = GetModel(sourceModelId);
		if (!sourceModel ||
			sourceModel->m_ContentGeneration != operation.m_ContentVersion.m_ContentGeneration)
		{
			m_AssetLoadCoordinator.CompleteMeshReload(operation);
			return;
		}

		if (artifact)
		{
			artifact = m_ModelImportArtifactCache.Admit(std::move(artifact));
		}
		m_AssetUploadScheduler->EnqueueCpuPayload(
			{
				.m_Name = completion.m_Name,
				.m_Identity =
					{
						.m_Kind = AssetStreamingWorkKind::Model,
						.m_StableId = sourceModelId.Value(),
						.m_Generation = operation.m_ContentVersion.m_ContentGeneration,
					},
				.m_Estimate =
					artifact ? EstimateImportedModel(*artifact) : AssetStreamingWorkEstimate{},
				.m_Priority = completion.m_Priority,
			},
			[this, operation, completion, artifact = std::move(artifact)]() mutable noexcept
			{ CompleteMeshReload(operation, completion, std::move(artifact)); });
	}

	void AssetManager::CompleteModelLoad(AssetOperationToken operation,
		const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentModelImport(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		m_AssetLoadCoordinator.CompleteModelImport(operation);
		const ModelID modelId{ static_cast<uint32_t>(operation.m_ContentVersion.m_Key.m_StableId) };
		const uint64_t generation = operation.m_ContentVersion.m_ContentGeneration;
		Model* model = EditModel(modelId);
		if (!model || model->m_ContentGeneration != generation)
		{
			return;
		}
		if (model->m_CancelRequested)
		{
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress)
				.Report(0.05f, "Model import cancelled", completion.m_Name);
			return;
		}

		if (completion.m_Status == TaskStatus::Cancelled)
		{
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress)
				.Report(0.05f, "Model import cancelled", completion.m_Name);
			return;
		}
		if (completion.m_Status != TaskStatus::Succeeded || !artifact || !artifact->IsValid())
		{
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress)
				.Report(0.05f, "Model import failed", completion.m_Error);
			GGLAB_LOG_GRAPHICS_ERROR(
				"Async model import '{}' failed: {}", completion.m_Name, completion.m_Error);
			return;
		}

		SetAssetState(*model, AssetState::CpuReady);
		ProgressReporter(model->m_LoadProgress)
			.Report(0.62f, "Queued for resource publication", completion.m_Name);
		model->m_ImportArtifactContentDigest = artifact->m_ContentDigest;
		const AssetStreamingWorkEstimate estimate = EstimateImportedModel(*artifact);
		const TaskPriority priority =
			GetEffectivePriority(MakeAssetKey(modelId), completion.m_Priority);
		auto publicationJob =
			std::make_unique<ModelPublicationJob>(CreateModelPublicationServices(),
				MakeAssetContentVersion(modelId, generation), completion.m_QueueMilliseconds,
				completion.m_ExecutionMilliseconds, std::move(artifact));
		m_AssetUploadScheduler->EnqueueResourcePublication(
			{
				.m_Name = std::format("Model {}", modelId.Value()),
				.m_Identity =
					{
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

	void AssetManager::CompleteMeshReload(AssetOperationToken operation,
		const TaskCompletionInfo& completion, ModelImportArtifactHandle artifact) noexcept
	{
		if (!m_AssetLoadCoordinator.IsCurrentMeshReload(operation) ||
			operation.m_ContentVersion.m_Key.m_Kind != AssetKind::Model)
		{
			return;
		}
		m_AssetLoadCoordinator.CompleteMeshReload(operation);
		const ModelID sourceModelId{
			static_cast<uint32_t>(operation.m_ContentVersion.m_Key.m_StableId) };
		Model* sourceModel = EditModel(sourceModelId);
		if (!sourceModel ||
			sourceModel->m_ContentGeneration != operation.m_ContentVersion.m_ContentGeneration)
		{
			return;
		}
		if (completion.m_Status == TaskStatus::Succeeded && artifact && artifact->IsValid() &&
			sourceModel->m_ImportArtifactContentDigest.IsValid() &&
			artifact->m_ContentDigest != sourceModel->m_ImportArtifactContentDigest)
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"Rejected mesh residency reload for immutable model generation {} (expected artifact {}, resolved artifact {}).",
				operation.m_ContentVersion.m_ContentGeneration,
				ArtifactContentDigestText(sourceModel->m_ImportArtifactContentDigest),
				ArtifactContentDigestText(artifact->m_ContentDigest));
			artifact.reset();
		}
		const ModelImportArtifact* importedModel = artifact.get();

		for (const auto& [meshId, meshOwner] : m_MeshStore.Entries())
		{
			Mesh* mesh = meshOwner.get();
			if (!mesh->m_IsReloading || mesh->m_SourceModelId != sourceModelId)
			{
				continue;
			}
			const AssetResidencyOperation residencyOperation{
				.m_Token = MakeAssetOperationToken(
					MakeAssetContentVersion(meshId, mesh->m_ContentGeneration),
					mesh->m_ResidencyOperationSerial),
				.m_Kind = AssetResidencyOperationKind::Reload,
			};
			if (!AssetResidencyController::IsCurrentOperation(*mesh, residencyOperation))
			{
				m_AssetResidencyController.RecordStaleCompletion();
				continue;
			}
			if (completion.m_Status != TaskStatus::Succeeded || !importedModel ||
				mesh->m_SourceMeshIndex >= importedModel->m_Meshes.size())
			{
				mesh->m_IsReloading = false;
				SetMeshState(*mesh, AssetState::CpuReady, residencyOperation,
					AssetStateEventOperationPhase::Completes);
				continue;
			}
			MeshUploadData uploadData{
				.m_MeshId = meshId,
				.m_ModelSource =
					{
						.m_Owner = artifact,
						.m_MeshIndex = mesh->m_SourceMeshIndex,
					},
			};
			if (!QueueMeshUpload(std::move(uploadData),
				GetEffectivePriority(MakeAssetKey(meshId), completion.m_Priority),
				residencyOperation))
			{
				mesh->m_IsReloading = false;
				SetMeshState(*mesh, AssetState::CpuReady, residencyOperation,
					AssetStateEventOperationPhase::Completes);
			}
		}
	}

	MeshID AssetManager::CreateMesh() noexcept
	{
		const MeshID meshId = m_MeshStore.Create();
		Mesh* mesh = EditMesh(meshId);
		GGLAB_ASSERT_NOT_NULL(mesh);
		BeginAssetContentGeneration(*mesh, 1, AssetState::LoadingCpu);
		mesh->m_LoadProgress = std::make_shared<ProgressChannel>();

		return meshId;
	}

	ModelID AssetManager::CreateModel(
		const std::filesystem::path& canonicalPath, AssetState initialState) noexcept
	{
		const ModelID modelId = m_ModelStore.Create(canonicalPath);
		Model* model = EditModel(modelId);
		GGLAB_ASSERT_NOT_NULL(model);
		if (!model)
		{
			return {};
		}
		BeginAssetContentGeneration(*model, 1, initialState);
		model->m_SourcePath = canonicalPath;
		model->m_LoadProgress = std::make_shared<ProgressChannel>();
		ProgressReporter(model->m_LoadProgress)
			.Report(initialState == AssetState::Queued ? 0.05f : 0.0f,
				initialState == AssetState::Queued ? "Queued for model import"
				: "Model entry created",
				canonicalPath.filename().generic_string());
		return modelId;
	}

	ModelID AssetManager::FindModel(const std::filesystem::path& canonicalPath) const noexcept
	{
		return m_ModelStore.FindByPath(canonicalPath);
	}

	bool AssetManager::DetachTerminalModelPath(
		const std::filesystem::path& canonicalPath, ModelID modelId) noexcept
	{
		if (!m_ModelStore.DetachPath(canonicalPath, modelId))
		{
			return false;
		}

		m_AssetLoadCoordinator.DiscardModelImport(MakeAssetKey(modelId));
		m_PendingModels.erase(modelId);
		GGLAB_LOG_GRAPHICS_INFO(
			"Detached terminal model {} from cache path '{}' so a later request can retry.",
			modelId.Value(), canonicalPath.string());
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
				const Texture* texture = m_TextureAssets->GetTexture(textureId);
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
		ModelID modelId, uint64_t generation, ModelDependencyOutcome traversalOutcome) noexcept
	{
		++m_DependencyValidationCount;
		const AssetContentVersion modelVersion = MakeAssetContentVersion(modelId, generation);
		const bool hasMatchingState = m_AssetDependencyGraph.FindModel(modelVersion) != nullptr;
		const ModelDependencyOutcome eventOutcome =
			hasMatchingState ? m_AssetDependencyGraph.EvaluateModel(modelVersion)
			: ModelDependencyOutcome::Failed;
		if (hasMatchingState && eventOutcome == traversalOutcome)
		{
			return;
		}

		++m_DependencyValidationMismatchCount;
		GGLAB_LOG_GRAPHICS_ERROR(
			"Model {} dependency tracking mismatch (generation={}, graph={}, traversal={}, graphPresent={}).",
			modelId.Value(), generation, static_cast<uint32_t>(eventOutcome),
			static_cast<uint32_t>(traversalOutcome), hasMatchingState);
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
			const AssetContentVersion modelVersion =
				MakeAssetContentVersion(modelId, model->m_ContentGeneration);
			if (!m_AssetDependencyGraph.FindModel(modelVersion) ||
				m_AssetDependencyGraph.EvaluateModel(modelVersion) == ModelDependencyOutcome::Ready)
			{
				return true;
			}
		}

		const ModelDependencyOutcome outcome = EvaluateModelDependenciesByTraversal(*model);
		VerifyModelDependencyState(modelId, model->m_ContentGeneration, outcome);
		switch (outcome)
		{
		case ModelDependencyOutcome::Failed:
			SetAssetState(*model, AssetState::Failed);
			ProgressReporter(model->m_LoadProgress).Report(0.96f, "Model dependency failed");
			UnregisterModelDependencies(modelId, model->m_ContentGeneration);
			return true;

		case ModelDependencyOutcome::Cancelled:
			SetAssetState(*model, AssetState::Cancelled);
			ProgressReporter(model->m_LoadProgress).Report(0.96f, "Model dependency cancelled");
			UnregisterModelDependencies(modelId, model->m_ContentGeneration);
			return true;

		case ModelDependencyOutcome::Pending:
			SetAssetState(*model, AssetState::GpuProcessing);
			ProgressReporter(model->m_LoadProgress)
				.Report(0.82f, "Waiting for model GPU dependencies");
			return false;

		case ModelDependencyOutcome::Ready:
			break;
		}

		SetAssetState(*model, AssetState::Ready);
		ProgressReporter(model->m_LoadProgress).Report(1.0f, "Model ready");
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

	void AssetManager::SetMaterialTexture(Material& material, MaterialTextureSlot slot,
		const MaterialTextureBinding& binding) noexcept
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
