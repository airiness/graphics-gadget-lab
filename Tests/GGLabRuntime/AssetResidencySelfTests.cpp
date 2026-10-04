#include "AssetResidencySelfTests.h"
#include "AssetTestServices.h"

#include "GGLabFoundation/Task/TaskSystem.h"
#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/ReservedTexture.h"

#include <algorithm>
#include <array>
#include <string>

namespace gglab
{
	namespace
	{
		using asset_test::AssetManagerHarness;
		using asset_test::FindMesh;
		using asset_test::FindModel;
		using asset_test::FindTexture;
		using asset_test::IsTerminal;

		constexpr const char* ResidencyModelPath = "Residency.gltf";

		// Residency transitions are asserted on stable IDs; generation changes would
		// mean a replaced entry rather than a residency transition.
		struct ResidencyIdentity
		{
			ModelID m_ModelId{};
			MeshID m_MeshId{};
			MaterialID m_MaterialId{};
			TextureID m_TextureId{};
			uint64_t m_ModelGeneration = 0;
			uint64_t m_MeshGeneration = 0;
			uint64_t m_TextureGeneration = 0;
		};

		[[nodiscard]] bool IsUnchanged(AssetManagerHarness& harness, const ResidencyIdentity& id) noexcept
		{
			const Model* model = harness.GetAssets().GetModel(id.m_ModelId);
			const Mesh* mesh = harness.GetAssets().GetMesh(id.m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
			const AssetSnapshot::Texture* texture = FindTexture(snapshot, id.m_TextureId);
			return model && mesh && texture && model->m_ContentGeneration == id.m_ModelGeneration &&
				mesh->m_ContentGeneration == id.m_MeshGeneration &&
				texture->m_ContentGeneration == id.m_TextureGeneration;
		}

		[[nodiscard]] TextureID FindFirstRuntimeTexture(const Material& material) noexcept
		{
			for (const TextureID textureId : std::array{ material.m_BaseColorBinding.m_TextureId,
				material.m_MetallicRoughnessBinding.m_TextureId, material.m_NormalBinding.m_TextureId,
				material.m_OcclusionBinding.m_TextureId, material.m_EmissiveBinding.m_TextureId })
			{
				if (textureId.IsValid() && !IsReservedTextureId(textureId))
				{
					return textureId;
				}
			}
			return {};
		}

		[[nodiscard]] AssetSnapshot::Texture SnapshotTexture(
			AssetManagerHarness& harness, TextureID textureId) noexcept
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(harness.GetAssets());
			const AssetSnapshot::Texture* texture = FindTexture(snapshot, textureId);
			return texture ? *texture : AssetSnapshot::Texture{};
		}

		void SetAutomaticEviction(AssetManagerHarness& harness, bool enabled) noexcept
		{
			AssetResidencyConfig config = harness.GetAssets().GetResidencyConfig();
			config.m_EnableAutomaticEviction = enabled;
			harness.GetAssets().SetResidencyConfig(config);
		}

		// Waits for a released texture to leave Evicting and checks it kept its content
		// while dropping GPU residency.
		[[nodiscard]] bool WaitForNonResidentTexture(
			AssetManagerHarness& harness, TextureID textureId, uint64_t generation) noexcept
		{
			const bool settled = harness.PumpUntil([&]()
				{
					const AssetSnapshot::Texture texture = SnapshotTexture(harness, textureId);
					return texture.m_Id == textureId &&
						texture.m_ResidencyState != AssetResidencyState::Evicting &&
						texture.m_ResidencyState != AssetResidencyState::Resident;
				});
			const AssetSnapshot::Texture texture = SnapshotTexture(harness, textureId);
			return settled && texture.m_ContentGeneration == generation &&
				texture.m_State == AssetState::CpuReady &&
				texture.m_ResidencyState == AssetResidencyState::NonResident &&
				!texture.m_IsUploaded && !texture.m_Texture.IsValid() && !texture.m_HasSrv;
		}

		[[nodiscard]] bool WaitForReadyTexture(AssetManagerHarness& harness, TextureID textureId) noexcept
		{
			return harness.PumpUntil([&]()
				{
					const AssetSnapshot::Texture texture = SnapshotTexture(harness, textureId);
					return texture.m_Id == textureId && IsTerminal(texture.m_State);
				}) && SnapshotTexture(harness, textureId).m_State == AssetState::Ready;
		}

		void RunResidencyLifecycleTest(SelfTestContext& context) noexcept
		{
			AssetManagerHarness harness("asset-residency");
			// A large source keeps its decode task observable while it runs.
			const std::array<std::string, 1> images{ "Residency.BaseColor.png" };
			const bool fixtures = harness.IsValid() &&
				asset_test::WriteNoisePng(harness.GetAssetRoot() / images[0], 1024) &&
				asset_test::WriteTexturedModel(harness.GetAssetRoot() / ResidencyModelPath, images);
			context.Check(fixtures, "Residency composes the harness and its model fixture");
			if (!fixtures)
			{
				return;
			}
			AssetManager& assets = harness.GetAssets();
			AssetResidencyConfig config = assets.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			config.m_HighWatermarkBytes = 1;
			config.m_LowWatermarkBytes = 0;
			config.m_MinUnusedFrames = 0;
			config.m_MaxEvictionsPerFrame = 16;
			config.m_RuntimeEntryRetentionFrames = 1'000'000;
			assets.SetResidencyConfig(config);

			ResidencyIdentity id{};
			auto owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::ModelLoadRequest request =
				owner->LoadModelAsync(ResidencyModelPath, TaskPriority::Normal);
			id.m_ModelId = request.m_ModelId;
			const bool loaded = request.IsValid() && harness.PumpUntil([&]()
				{
					const Model* model = assets.GetModel(request.m_ModelId);
					return model && model->m_ContentGeneration == request.m_Generation &&
						IsTerminal(model->m_State) &&
						asset_test::IsStreamingIdle(harness.GetScheduling().GetStatistics());
				});
			const Model* model = assets.GetModel(request.m_ModelId);
			context.Check(loaded && model && model->m_State == AssetState::Ready &&
				model->m_ContentState == AssetContentState::Ready &&
				model->m_ResidencyState == AssetResidencyState::Resident &&
				model->m_ResidencyEpoch != 0 && !model->m_MeshInstance.empty(),
				"A Ready model publishes valid content and resident residency metadata");
			if (!loaded || !model || model->m_MeshInstance.empty())
			{
				return;
			}

			// Loaded dependency graph, shared texture derived data and import artifact caching.
			id.m_MeshId = model->m_MeshInstance.front().m_MeshId;
			id.m_MaterialId = model->m_MeshInstance.front().m_MaterialId;
			const Mesh* mesh = assets.GetMesh(id.m_MeshId);
			const Material* material = assets.GetMaterial(id.m_MaterialId);
			id.m_TextureId = material ? FindFirstRuntimeTexture(*material) : TextureID{};
			const AssetSnapshot loadedSnapshot = BuildAssetSnapshot(assets);
			const AssetSnapshot::Texture* texture = FindTexture(loadedSnapshot, id.m_TextureId);
			const AssetSnapshot::Model* modelSnapshot = FindModel(loadedSnapshot, id.m_ModelId);
			context.Check(mesh && mesh->m_ResidencyState == AssetResidencyState::Resident && texture &&
				texture->m_ResidencyState == AssetResidencyState::Resident,
				"The Ready model keeps resident mesh and texture dependencies");
			if (!mesh || !texture || !modelSnapshot)
			{
				return;
			}
			context.Check(loadedSnapshot.m_ResourcePublicationQueue.m_SourceBytesCopiedToUpload == 0,
				"Model publication borrows immutable artifact data instead of copying CPU upload payloads");
			context.Check(texture->m_SourceDigest.IsValid() && texture->m_DerivedDataKey.IsValid() &&
				texture->m_IsDerivedDataCached,
				"Model textures resolve through the shared texture derived-data path");
			context.Check(modelSnapshot->m_HasDependencyState && modelSnapshot->m_DependencyCount != 0 &&
				modelSnapshot->m_ReadyDependencyCount == modelSnapshot->m_DependencyCount &&
				modelSnapshot->m_PendingDependencyCount == 0 && modelSnapshot->m_FailedDependencyCount == 0 &&
				modelSnapshot->m_CancelledDependencyCount == 0 &&
				loadedSnapshot.m_TrackedModelDependencyCount != 0 &&
				loadedSnapshot.m_ReverseDependencyEdgeCount >= modelSnapshot->m_DependencyCount &&
				loadedSnapshot.m_DependencyValidationCount != 0 &&
				loadedSnapshot.m_DependencyValidationMismatchCount == 0,
				"The model dependency graph converges with traversal-based readiness");
			context.Check(modelSnapshot->m_ImportArtifactContentDigest.IsValid() &&
				modelSnapshot->m_IsImportArtifactCached &&
				loadedSnapshot.m_ModelImportArtifactCachedEntryCount != 0 &&
				loadedSnapshot.m_ModelImportArtifactAdmissionCount != 0,
				"The immutable model import artifact is admitted to the CPU cache");

			// Residency policies.
			context.Check(assets.SetModelResidencyPolicy(id.m_ModelId, AssetResidencyPolicy::Pinned) &&
				assets.SetModelResidencyPolicy(id.m_ModelId, AssetResidencyPolicy::Cacheable) &&
				assets.SetMeshResidencyPolicy(id.m_MeshId, AssetResidencyPolicy::Pinned) &&
				assets.SetMeshResidencyPolicy(id.m_MeshId, AssetResidencyPolicy::Cacheable) &&
				assets.SetTextureResidencyPolicy(id.m_TextureId, AssetResidencyPolicy::Pinned) &&
				assets.SetTextureResidencyPolicy(id.m_TextureId, AssetResidencyPolicy::Cacheable),
				"Cacheable assets accept valid residency policy transitions");
			const TextureID reservedTexture = ToTextureId(ReservedTextureIDIndex::BaseColorWhite);
			const bool reservedRejected =
				!assets.SetTextureResidencyPolicy(reservedTexture, AssetResidencyPolicy::Cacheable);
			const AssetSnapshot::Texture reserved = SnapshotTexture(harness, reservedTexture);
			context.Check(reservedRejected && reserved.m_ResidencyPolicy == AssetResidencyPolicy::Pinned,
				"Reserved textures stay pinned and reject cacheable residency");
			context.Check(assets.SetModelResidencyPolicy(id.m_ModelId, AssetResidencyPolicy::Pinned),
				"The model can be pinned for eviction protection");

			id.m_ModelGeneration = model->m_ContentGeneration;
			id.m_MeshGeneration = mesh->m_ContentGeneration;
			id.m_TextureGeneration = texture->m_ContentGeneration;
			const TextureImportSettings textureImportSettings = texture->m_ImportSettings;
			const std::filesystem::path textureSourcePath = texture->m_SourcePath;
			const uint64_t meshResidencyEpoch = mesh->m_ResidencyEpoch;
			const uint64_t textureResidencyEpoch = texture->m_ResidencyEpoch;
			const AssetResidencyStatistics baseline = assets.GetResidencyStatistics();
			const uint64_t importArtifactHits = loadedSnapshot.m_ModelImportArtifactCacheHitCount;

			// Per-frame usage is deduplicated.
			const uint64_t modelUses = model->m_UseCount;
			const uint64_t meshUses = mesh->m_UseCount;
			const uint64_t textureUses = texture->m_UseCount;
			for (uint32_t repeat = 0; repeat < 2; ++repeat)
			{
				assets.MarkModelUsed(id.m_ModelId);
				assets.MarkMeshUsed(id.m_MeshId);
				assets.MarkTextureUsed(id.m_TextureId);
			}
			const AssetSnapshot usage = BuildAssetSnapshot(assets);
			const AssetSnapshot::Model* usedModel = FindModel(usage, id.m_ModelId);
			const AssetSnapshot::Mesh* usedMesh = FindMesh(usage, id.m_MeshId);
			const AssetSnapshot::Texture* usedTexture = FindTexture(usage, id.m_TextureId);
			context.Check(usedModel && usedMesh && usedTexture &&
				usedModel->m_UseCount == modelUses + 1 && usedMesh->m_UseCount == meshUses + 1 &&
				usedTexture->m_UseCount == textureUses + 1 &&
				usedModel->m_LastUsedFrame == usage.m_AssetUsageFrame &&
				usedMesh->m_LastUsedFrame == usage.m_AssetUsageFrame &&
				usedTexture->m_LastUsedFrame == usage.m_AssetUsageFrame,
				"Per-frame asset usage tracking deduplicates repeated marks");

			// A pinned model protects its dependencies from automatic eviction and retirement.
			owner->Reset();
			config = assets.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			config.m_RuntimeEntryRetentionFrames = 0;
			config.m_MaxRuntimeRetirementsPerFrame = 64;
			assets.SetResidencyConfig(config);
			harness.PumpFrames(8);
			const Model* pinnedModel = assets.GetModel(id.m_ModelId);
			const Mesh* pinnedMesh = assets.GetMesh(id.m_MeshId);
			const AssetSnapshot::Texture pinnedTexture = SnapshotTexture(harness, id.m_TextureId);
			const AssetResidencyStatistics pinnedResidency = assets.GetResidencyStatistics();
			context.Check(IsUnchanged(harness, id) && pinnedModel &&
				pinnedModel->m_ResidencyPolicy == AssetResidencyPolicy::Pinned &&
				pinnedModel->m_State == AssetState::Ready && pinnedMesh &&
				pinnedMesh->m_State == AssetState::Ready &&
				pinnedMesh->m_ResidencyState == AssetResidencyState::Resident &&
				pinnedTexture.m_State == AssetState::Ready &&
				pinnedTexture.m_ResidencyState == AssetResidencyState::Resident &&
				pinnedResidency.m_EvictionCount == baseline.m_EvictionCount &&
				pinnedResidency.m_PendingEvictionCount == 0,
				"A pinned model protects its resident dependencies from eviction and retirement");

			config = assets.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			config.m_RuntimeEntryRetentionFrames = 1'000'000;
			assets.SetResidencyConfig(config);
			context.Check(assets.SetModelResidencyPolicy(id.m_ModelId, AssetResidencyPolicy::Cacheable),
				"The model returns to cacheable residency");
			harness.PumpFrames(1);
			const AssetSnapshot candidates = BuildAssetSnapshot(assets);
			const AssetSnapshot::Model* candidateModel = FindModel(candidates, id.m_ModelId);
			const AssetSnapshot::Mesh* candidateMesh = FindMesh(candidates, id.m_MeshId);
			const AssetSnapshot::Texture* candidateTexture = FindTexture(candidates, id.m_TextureId);
			const AssetOwnershipStatistics cachedOwnership = assets.GetOwnershipStatistics();
			const auto hasDependencyInterest = [&](AssetKind kind, uint64_t stableId, uint64_t generation)
				{
					const AssetInterestActivity* interest =
						asset_test::FindInterest(cachedOwnership, kind, stableId);
					return interest && interest->m_Generation == generation;
				};
			context.Check(candidateModel && candidateMesh && candidateTexture &&
				candidateModel->m_IsEvictionCandidate && candidateMesh->m_IsEvictionCandidate &&
				candidateTexture->m_IsEvictionCandidate && candidateModel->m_HasDependencyState &&
				candidates.m_DependencyValidationMismatchCount == 0,
				"Unowned cacheable resident assets are classified as eviction candidates");
			context.Check(hasDependencyInterest(AssetKind::Mesh, id.m_MeshId.Value(), id.m_MeshGeneration) &&
				hasDependencyInterest(AssetKind::Texture, id.m_TextureId.Value(), id.m_TextureGeneration),
				"Cached model dependencies stay owned until the model's runtime retirement");

			// Reacquiring interest cancels an eviction before its fence-safe release.
			harness.GetDevice().SetFencesComplete(false);
			SetAutomaticEviction(harness, true);
			const bool evicting = harness.PumpUntil([&]()
				{
					const Mesh* evictingMesh = assets.GetMesh(id.m_MeshId);
					const AssetSnapshot::Texture evictingTexture = SnapshotTexture(harness, id.m_TextureId);
					return evictingMesh && evictingMesh->m_ResidencyState == AssetResidencyState::Evicting &&
						evictingTexture.m_ResidencyState == AssetResidencyState::Evicting;
				});
			context.Check(evicting && IsUnchanged(harness, id),
				"Eviction of unowned cacheable dependencies starts without replacing stable entries");
			owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::ModelLoadRequest revived =
				owner->LoadModelAsync(ResidencyModelPath, TaskPriority::Critical);
			context.Check(revived.IsValid() && revived.m_ModelId == id.m_ModelId &&
				revived.m_Generation == id.m_ModelGeneration,
				"Reacquiring an evicting model preserves its content version");
			harness.GetDevice().SetFencesComplete(true);
			const bool revivedReady = harness.PumpUntil([&]()
				{
					const Model* revivedModel = assets.GetModel(id.m_ModelId);
					const Mesh* revivedMesh = assets.GetMesh(id.m_MeshId);
					const AssetSnapshot::Texture revivedTexture = SnapshotTexture(harness, id.m_TextureId);
					return revivedModel && revivedMesh && IsTerminal(revivedModel->m_State) &&
						IsTerminal(revivedMesh->m_State) && IsTerminal(revivedTexture.m_State);
				});
			const Mesh* revivedMesh = assets.GetMesh(id.m_MeshId);
			const AssetSnapshot::Texture revivedTexture = SnapshotTexture(harness, id.m_TextureId);
			const AssetResidencyStatistics cancelled = assets.GetResidencyStatistics();
			context.Check(revivedReady && revivedMesh && revivedMesh->m_State == AssetState::Ready &&
				revivedTexture.m_State == AssetState::Ready &&
				cancelled.m_EvictionCancellationCount >= baseline.m_EvictionCancellationCount + 2 &&
				cancelled.m_EvictionCount == baseline.m_EvictionCount &&
				cancelled.m_OperationCount >= baseline.m_OperationCount + 4 &&
				cancelled.m_StaleCompletionCount >= baseline.m_StaleCompletionCount + 2 &&
				revivedMesh->m_ResidencyState == AssetResidencyState::Resident &&
				revivedTexture.m_ResidencyState == AssetResidencyState::Resident &&
				revivedMesh->m_IsUploaded && revivedTexture.m_IsUploaded &&
				BuildAssetSnapshot(assets).m_DependencyValidationMismatchCount == 0,
				"Reacquiring interest cancels pending eviction before any resource release");

			// Release drops GPU residency but keeps content and the CPU artifact cache.
			owner->Reset();
			const bool released = harness.PumpUntil([&]()
				{
					const Mesh* releasedMesh = assets.GetMesh(id.m_MeshId);
					const AssetSnapshot::Texture releasedTexture = SnapshotTexture(harness, id.m_TextureId);
					return releasedMesh && releasedMesh->m_ResidencyState == AssetResidencyState::NonResident &&
						releasedTexture.m_ResidencyState == AssetResidencyState::NonResident;
				});
			const Mesh* releasedMesh = assets.GetMesh(id.m_MeshId);
			const AssetSnapshot::Texture releasedTexture = SnapshotTexture(harness, id.m_TextureId);
			const TextureArtifactCacheStatistics artifactCache = assets.GetTextureArtifactCacheStatistics();
			context.Check(released && IsUnchanged(harness, id) && releasedMesh &&
				releasedMesh->m_State == AssetState::CpuReady &&
				releasedMesh->m_ContentState == AssetContentState::Ready && !releasedMesh->m_IsUploaded &&
				!releasedMesh->m_VertexBuffer && !releasedMesh->m_IndexBuffer &&
				releasedTexture.m_State == AssetState::CpuReady &&
				releasedTexture.m_ContentState == AssetContentState::Ready &&
				!releasedTexture.m_IsUploaded && !releasedTexture.m_Texture.IsValid() &&
				!releasedTexture.m_HasSrv &&
				assets.GetResidencyStatistics().m_EvictionCount >= baseline.m_EvictionCount + 2,
				"Released assets preserve content while their GPU residency is finalized");
			context.Check(releasedTexture.m_IsCpuArtifactCached &&
				releasedTexture.m_ArtifactContentDigest.IsValid() &&
				artifactCache.m_CachedEntryCount != 0 && artifactCache.m_CachedBytes != 0,
				"The decoded texture is retained by the CPU artifact cache");

			// Reload from the CPU artifact cache without a decode task.
			SetAutomaticEviction(harness, false);
			owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::TextureLoadRequest cacheReload = owner->LoadTextureAsync(
				textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Critical);
			context.Check(cacheReload.IsValid() && !cacheReload.m_Task.IsValid() &&
				cacheReload.m_TextureId == id.m_TextureId &&
				cacheReload.m_Generation == id.m_TextureGeneration &&
				assets.GetTextureArtifactCacheStatistics().m_HitCount == artifactCache.m_HitCount + 1,
				"A texture residency reload hits its cached CPU artifact without a decode task");
			const bool cacheReady = WaitForReadyTexture(harness, id.m_TextureId);
			const AssetSnapshot::Texture cacheTexture = SnapshotTexture(harness, id.m_TextureId);
			context.Check(cacheReady && cacheTexture.m_ResidencyState == AssetResidencyState::Resident &&
				cacheTexture.m_IsUploaded && cacheTexture.m_Texture.IsValid() && cacheTexture.m_HasSrv &&
				cacheTexture.m_IsCpuArtifactCached,
				"The cached CPU artifact restores texture GPU residency");
			owner->Reset();
			SetAutomaticEviction(harness, true);
			context.Check(WaitForNonResidentTexture(harness, id.m_TextureId, id.m_TextureGeneration),
				"A cache-hit texture returns to non-resident state after release");

			// Source build publishes to the local DDC; a later reload hits it.
			assets.ClearTextureArtifactCache();
			context.Check(assets.GetTextureArtifactCacheStatistics().m_CachedEntryCount == 0 &&
				assets.ClearTextureDerivedDataCache(),
				"The texture CPU artifact cache and local DDC can be cleared");
			SetAutomaticEviction(harness, false);
			const uint64_t ddcWrites = assets.GetTextureDerivedDataStatistics().m_WriteCount;
			owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::TextureLoadRequest sourceBuild = owner->LoadTextureAsync(
				textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Critical);
			const bool built = sourceBuild.IsValid() && sourceBuild.m_Task.IsValid() &&
				sourceBuild.m_TextureId == id.m_TextureId &&
				sourceBuild.m_Generation == id.m_TextureGeneration &&
				WaitForReadyTexture(harness, id.m_TextureId);
			const AssetSnapshot::Texture builtTexture = SnapshotTexture(harness, id.m_TextureId);
			context.Check(built && builtTexture.m_DerivedDataKey.IsValid() &&
				builtTexture.m_SourceDigest.IsValid() && builtTexture.m_IsDerivedDataCached &&
				assets.GetTextureDerivedDataStatistics().m_WriteCount >= ddcWrites + 1,
				"A source-built texture is published to the local DDC");
			owner->Reset();
			SetAutomaticEviction(harness, true);
			context.Check(WaitForNonResidentTexture(harness, id.m_TextureId, id.m_TextureGeneration),
				"A DDC source-built texture becomes non-resident after release");

			assets.ClearTextureArtifactCache();
			SetAutomaticEviction(harness, false);
			const uint64_t ddcHits = assets.GetTextureDerivedDataStatistics().m_HitCount;
			owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::TextureLoadRequest ddcReload = owner->LoadTextureAsync(
				textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Critical);
			context.Check(ddcReload.IsValid() && ddcReload.m_Task.IsValid() &&
				WaitForReadyTexture(harness, id.m_TextureId) &&
				assets.GetTextureDerivedDataStatistics().m_HitCount >= ddcHits + 1,
				"A texture reload without a CPU artifact records a local DDC hit");
			owner->Reset();
			SetAutomaticEviction(harness, true);
			context.Check(WaitForNonResidentTexture(harness, id.m_TextureId, id.m_TextureGeneration),
				"A DDC-hit texture becomes non-resident after release");

			// Cancelling a running reload creates a replacement task that a stale
			// completion cannot remove.
			assets.ClearTextureArtifactCache();
			context.Check(assets.ClearTextureDerivedDataCache(),
				"The local DDC is cleared before the reload replacement probe");
			SetAutomaticEviction(harness, false);
			owner = std::make_unique<AssetOwnerScope>(assets.CreateOwnerScope());
			const AssetManager::TextureLoadRequest staleReload = owner->LoadTextureAsync(
				textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Background);
			const bool staleRunning = staleReload.IsValid() && staleReload.m_Task.IsValid() &&
				harness.PumpUntil([&]()
					{
						const TaskSystemStatistics tasks = harness.GetTaskSystem().GetStatistics();
						const auto activity = std::ranges::find(
							tasks.m_ActiveTasks, staleReload.m_Task, &TaskActivity::m_Handle);
						return activity != tasks.m_ActiveTasks.end() &&
							activity->m_Status == TaskStatus::Running;
					});
			owner->Reset();
			const AssetManager::TextureLoadRequest replacement = owner->LoadTextureAsync(
				textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Critical);
			context.Check(staleRunning && replacement.IsValid() && replacement.m_Task.IsValid() &&
				replacement.m_TextureId == id.m_TextureId &&
				replacement.m_Generation == id.m_TextureGeneration &&
				replacement.m_Task != staleReload.m_Task,
				"Cancelling a running texture reload creates a replacement task on reacquire");
			const bool staleDelivered = harness.PumpUntil([&]()
				{
					const TaskSystemStatistics tasks = harness.GetTaskSystem().GetStatistics();
					return std::ranges::any_of(tasks.m_RecentTasks,
						[&](const TaskCompletionInfo& completion) noexcept
						{ return completion.m_Handle == staleReload.m_Task; });
				});
			const TaskSystemStatistics afterStale = harness.GetTaskSystem().GetStatistics();
			const bool replacementFinished = std::ranges::any_of(afterStale.m_RecentTasks,
				[&](const TaskCompletionInfo& completion) noexcept
				{ return completion.m_Handle == replacement.m_Task; });
			if (!replacementFinished)
			{
				const AssetManager::TextureLoadRequest tracked = owner->LoadTextureAsync(
					textureSourcePath, textureImportSettings.m_Semantic, TaskPriority::Critical);
				context.Check(tracked.m_Task.IsValid() && tracked.m_Task == replacement.m_Task,
					"A stale texture completion keeps the replacement task record");
			}
			context.Check(staleDelivered && WaitForReadyTexture(harness, id.m_TextureId),
				"The replacement texture decode completes the residency reload");

			// Source-backed model reload restores residency through validated events.
			const AssetManager::ModelLoadRequest reloaded =
				owner->LoadModelAsync(ResidencyModelPath, TaskPriority::Normal);
			context.Check(reloaded.IsValid() && reloaded.m_ModelId == id.m_ModelId &&
				reloaded.m_Generation == id.m_ModelGeneration,
				"Reload preserves the model ID and content generation");
			const bool reloadReady = harness.PumpUntil([&]()
				{
					const Model* reloadedModel = assets.GetModel(id.m_ModelId);
					const Mesh* reloadedMesh = assets.GetMesh(id.m_MeshId);
					const AssetSnapshot::Texture reloadedTexture = SnapshotTexture(harness, id.m_TextureId);
					return reloadedModel && reloadedMesh && reloadedModel->m_State == AssetState::Ready &&
						reloadedMesh->m_State == AssetState::Ready &&
						reloadedTexture.m_State == AssetState::Ready &&
						asset_test::IsStreamingIdle(harness.GetScheduling().GetStatistics());
				});
			const TextureContentRef textureContent = assets.GetTextureContentRef(id.m_TextureId);
			TextureContentRef staleContent = textureContent;
			++staleContent.m_Generation;
			context.Check(reloadReady && textureContent.IsValid() &&
				textureContent.m_Generation == id.m_TextureGeneration &&
				assets.GetResidentTextureResource(textureContent).has_value() &&
				!assets.GetResidentTextureResource(staleContent).has_value(),
				"Resident texture render views exist only for the current content generation");
			const Mesh* reloadedMesh = assets.GetMesh(id.m_MeshId);
			const AssetSnapshot reloadSnapshot = BuildAssetSnapshot(assets);
			const AssetSnapshot::Texture* reloadedTexture = FindTexture(reloadSnapshot, id.m_TextureId);
			const AssetSnapshot::Model* reloadedModel = FindModel(reloadSnapshot, id.m_ModelId);
			const AssetResidencyStatistics reloadResidency = assets.GetResidencyStatistics();
			context.Check(IsUnchanged(harness, id) && reloadedMesh && reloadedTexture &&
				reloadedTexture->m_ImportSettings == textureImportSettings &&
				reloadedMesh->m_ResidencyEpoch > meshResidencyEpoch &&
				reloadedTexture->m_ResidencyEpoch > textureResidencyEpoch &&
				reloadedMesh->m_ResidencyOperationSerial == 0 &&
				reloadedTexture->m_ResidencyOperationSerial == 0 &&
				reloadedMesh->m_IsUploaded && reloadedTexture->m_IsUploaded &&
				reloadResidency.m_ReloadRequestCount > baseline.m_ReloadRequestCount &&
				reloadResidency.m_ReloadingAssetCount == 0 &&
				reloadResidency.m_AcceptedStateEventCount > baseline.m_AcceptedStateEventCount &&
				reloadResidency.m_CompletedStateEventCount >= baseline.m_CompletedStateEventCount + 6 &&
				reloadResidency.m_AcceptedStateEventCount >= reloadResidency.m_CompletedStateEventCount &&
				reloadSnapshot.m_DependencyValidationMismatchCount == 0,
				"Reload restores residency through validated state-operation events on stable IDs");
			context.Check(reloadedModel && reloadedModel->m_IsImportArtifactCached &&
				reloadedModel->m_ImportArtifactContentDigest.IsValid() &&
				reloadSnapshot.m_ModelImportArtifactCacheHitCount > importArtifactHits,
				"Mesh residency reload reuses the immutable model import artifact");

			// Owner-scoped release retires every runtime entry.
			config = assets.GetResidencyConfig();
			config.m_RuntimeEntryRetentionFrames = 0;
			config.m_MaxRuntimeRetirementsPerFrame = 64;
			assets.SetResidencyConfig(config);
			const uint64_t retirements = assets.GetOwnershipStatistics().m_RuntimeRetirementCount;
			owner.reset();
			const bool retired = harness.PumpUntil([&]()
				{
					return !assets.GetModel(id.m_ModelId) && !assets.GetMesh(id.m_MeshId) &&
						!assets.GetMaterial(id.m_MaterialId) &&
						!FindTexture(BuildAssetSnapshot(assets), id.m_TextureId);
				});
			const TextureContentRef retiredTexture{ .m_Id = id.m_TextureId, .m_Generation = id.m_TextureGeneration };
			const AssetOwnershipStatistics retiredOwnership = assets.GetOwnershipStatistics();
			context.Check(retired && !assets.GetTextureState(retiredTexture) &&
				!assets.GetResidentTextureResource(retiredTexture) &&
				retiredOwnership.m_RuntimeRetirementCount >= retirements + 3 &&
				retiredOwnership.m_PendingRuntimeRetirementCount == 0,
				"Owner-scoped retirement leaves no addressable or pending runtime entry");

			// A direct load without an owner retires once its retention window elapses.
			config.m_RuntimeEntryRetentionFrames = 1'000'000;
			assets.SetResidencyConfig(config);
			const AssetManager::ModelLoadRequest direct =
				assets.LoadModelAsync(ResidencyModelPath, TaskPriority::Normal);
			const bool directReady = direct.IsValid() && harness.PumpUntil([&]()
				{
					const Model* directModel = assets.GetModel(direct.m_ModelId);
					return directModel && directModel->m_ContentGeneration == direct.m_Generation &&
						IsTerminal(directModel->m_State) &&
						asset_test::IsStreamingIdle(harness.GetScheduling().GetStatistics());
				});
			const Model* directModel = assets.GetModel(direct.m_ModelId);
			const bool directComplete = directReady && directModel &&
				directModel->m_State == AssetState::Ready && !directModel->m_MeshInstance.empty();
			context.Check(directComplete, "A direct model load without an owner becomes Ready");
			if (directComplete)
			{
				const ModelMesh instance = directModel->m_MeshInstance.front();
				const Material* directMaterial = assets.GetMaterial(instance.m_MaterialId);
				const TextureID directTexture =
					directMaterial ? FindFirstRuntimeTexture(*directMaterial) : TextureID{};
				const uint64_t directRetirements = assets.GetOwnershipStatistics().m_RuntimeRetirementCount;
				config.m_RuntimeEntryRetentionFrames = 0;
				assets.SetResidencyConfig(config);
				const bool directRetired = directTexture.IsValid() && harness.PumpUntil([&]()
					{
						return !assets.GetModel(direct.m_ModelId) && !assets.GetMesh(instance.m_MeshId) &&
							!assets.GetMaterial(instance.m_MaterialId) &&
							!FindTexture(BuildAssetSnapshot(assets), directTexture);
					});
				const AssetOwnershipStatistics directOwnership = assets.GetOwnershipStatistics();
				context.Check(directRetired &&
					directOwnership.m_RuntimeRetirementCount >= directRetirements + 3 &&
					directOwnership.m_PendingRuntimeRetirementCount == 0,
					"Direct-loaded runtime entries retire once their retention window elapses");
			}
			asset_test::CheckQuiescent(context, harness, "Residency lifecycle");
		}
	}

	void RunAssetResidencySelfTests(SelfTestContext& context) noexcept
	{
		RunResidencyLifecycleTest(context);
	}
}
