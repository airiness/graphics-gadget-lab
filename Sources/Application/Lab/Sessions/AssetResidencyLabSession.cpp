#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/AssetResidencyLabSession.h"
#include "Core/Task/TaskSystem.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/DerivedData/TextureDerivedDataSystem.h"
#include "Graphics/Asset/Loading/TextureLoader.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		[[nodiscard]] const AssetSnapshot::Model* FindModelSnapshot(
			const AssetSnapshot& snapshot,
			ModelID id) noexcept
		{
			const auto iterator = std::ranges::find(snapshot.m_Models, id, &AssetSnapshot::Model::m_Id);
			return iterator != snapshot.m_Models.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] const AssetSnapshot::Mesh* FindMeshSnapshot(
			const AssetSnapshot& snapshot,
			MeshID id) noexcept
		{
			const auto iterator = std::ranges::find(snapshot.m_Meshes, id, &AssetSnapshot::Mesh::m_Id);
			return iterator != snapshot.m_Meshes.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] const AssetSnapshot::Texture* FindTextureSnapshot(
			const AssetSnapshot& snapshot,
			TextureID id) noexcept
		{
			const auto iterator = std::ranges::find(snapshot.m_Textures, id, &AssetSnapshot::Texture::m_Id);
			return iterator != snapshot.m_Textures.end() ? &*iterator : nullptr;
		}

		[[nodiscard]] bool ValidateTextureDerivedDataCoordinatorContract(
			std::string& error) noexcept
		{
			TextureDerivedDataSystem system(std::filesystem::path{});
			SourceDigest sourceDigest{};
			sourceDigest.m_Value.front() = std::byte{ 0x5a };
			TextureImportSettings importSettings{};
			importSettings.m_Semantic = TextureSemantic::GenericColor;
			const DerivedDataKey key = BuildTextureDerivedDataKey(
				sourceDigest,
				"shared-request.png",
				importSettings);
			TextureDerivedDataRequestResult producer = system.Request(key);
			TextureDerivedDataRequestResult waiting = system.Request(key);
			if (producer.m_Disposition != ArtifactRequestDisposition::BuildRequired ||
				!producer.m_BuildClaim.IsValid() || !producer.m_Waiter.IsValid() ||
				waiting.m_Disposition != ArtifactRequestDisposition::Waiting ||
				!waiting.m_Waiter.IsValid())
			{
				error = "Texture shared request did not create one build claim and one waiter.";
				return false;
			}
			if (!producer.m_Waiter.Cancel())
			{
				error = "Cancelling one texture participant failed.";
				return false;
			}

			constexpr std::array<uint8_t, 4> pixels{ 64, 128, 192, 255 };
			TextureAssetData textureData = TextureLoader::MakeTexture2DRgba8(
				1,
				1,
				pixels,
				TextureColorSpace::SRGB);
			const AssetContentFingerprint contentFingerprint =
				ComputeTextureContentFingerprint(textureData, importSettings);
			TextureArtifactBuildResult built = CreateTextureArtifact(
				std::move(textureData));
			TextureArtifactHandle artifact = built.Succeeded() ?
				std::make_shared<const TextureArtifact>(std::move(built.m_Artifact)) :
				TextureArtifactHandle{};
			TextureDerivedDataArtifact published{
				.m_Artifact = artifact,
				.m_ContentFingerprint = contentFingerprint,
			};
			if (!published.IsValid() || !system.Publish(
				std::move(producer.m_BuildClaim),
				published))
			{
				error = "Texture shared request could not publish its artifact.";
				return false;
			}

			TextureDerivedDataRequestResult immediate = system.Request(key);
			TextureArtifactWaitResult waited = system.Wait(
				std::move(waiting.m_Waiter),
				{});
			const TextureDerivedDataCoordinatorStatistics statistics =
				system.GetCoordinatorStatistics();
			if (immediate.m_Disposition != ArtifactRequestDisposition::Hit ||
				immediate.m_Artifact.m_Artifact != artifact ||
				waited.m_Disposition != ArtifactWaitDisposition::Succeeded ||
				waited.m_Artifact.m_Artifact != artifact ||
				statistics.m_RequestCount != 3 ||
				statistics.m_BuildRequiredCount != 1 ||
				statistics.m_WaitCount != 1 ||
				statistics.m_ImmediateHitCount != 1 ||
				statistics.m_PublishCount != 1 ||
				statistics.m_CancelledWaiterCount != 1 ||
				statistics.m_FanoutDeliveryCount != 1 ||
				statistics.m_ActiveBuildCount != 0 ||
				statistics.m_ActiveWaiterCount != 0)
			{
				error = "Texture shared request fan-out or diagnostics did not satisfy the contract.";
				return false;
			}

			sourceDigest.m_Value.back() = std::byte{ 0xa5 };
			const DerivedDataKey cancellationKey = BuildTextureDerivedDataKey(
				sourceDigest,
				"cancelled-shared-request.png",
				importSettings);
			TextureDerivedDataRequestResult cancelledProducer = system.Request(cancellationKey);
			if (cancelledProducer.m_Disposition != ArtifactRequestDisposition::BuildRequired ||
				!cancelledProducer.m_Waiter.Cancel())
			{
				error = "Texture cancellation contract could not create and release its producer participant.";
				return false;
			}
			TextureDerivedDataRequestResult replacement = system.Request(cancellationKey);
			if (replacement.m_Disposition != ArtifactRequestDisposition::Waiting ||
				replacement.m_BuildClaim.IsValid())
			{
				error = "Texture participant cancellation allowed a second producer for an active key.";
				return false;
			}
			GGLAB_UNUSED(system.Fail(
				std::move(cancelledProducer.m_BuildClaim),
				"Expected cancellation-boundary validation failure."));
			if (system.Wait(std::move(replacement.m_Waiter), {}).m_Disposition !=
				ArtifactWaitDisposition::Failed)
			{
				error = "Texture cancellation-boundary waiter did not observe producer completion.";
				return false;
			}
			return true;
		}
	}

	struct AssetResidencyLabSession::State
	{
		enum class Phase : uint8_t
		{
			Loading,
			MarkUsage,
			WaitForPinnedProtection,
			ReleaseOwner,
			WaitForEvictionStart,
			WaitForEvictionCancellation,
			WaitForRelease,
			WaitForTextureArtifactCacheReload,
			WaitForTextureArtifactCacheRelease,
			WaitForTextureDdcBuild,
			WaitForTextureDdcBuildRelease,
			WaitForTextureDdcReload,
			WaitForTextureDdcReloadRelease,
			WaitForTextureReloadRunning,
			WaitForTextureReloadReplacement,
			WaitForTextureReloadCompletion,
			WaitForReload,
			Completed,
		};

		AssetManager::ModelLoadRequest m_Request{};
		AssetResidencyConfig m_OriginalResidencyConfig{};
		MeshID m_MeshId{};
		TextureID m_TextureId{};
		uint64_t m_ModelGeneration = 0;
		uint64_t m_MeshGeneration = 0;
		uint64_t m_TextureGeneration = 0;
		TextureImportSettings m_TextureImportSettings{};
		uint64_t m_MeshResidencyEpoch = 0;
		uint64_t m_TextureResidencyEpoch = 0;
		uint64_t m_EvictionCountBaseline = 0;
		uint64_t m_EvictionCancellationCountBaseline = 0;
		uint64_t m_ReloadRequestCountBaseline = 0;
		uint64_t m_OperationCountBaseline = 0;
		uint64_t m_StaleCompletionCountBaseline = 0;
		uint64_t m_AcceptedStateEventCountBaseline = 0;
		uint64_t m_CompletedStateEventCountBaseline = 0;
		uint64_t m_ModelImportArtifactCacheHitCountBaseline = 0;
		uint64_t m_TextureArtifactCacheHitCountBaseline = 0;
		uint64_t m_TextureDdcHitCountBaseline = 0;
		uint64_t m_TextureDdcWriteCountBaseline = 0;
		uint64_t m_PublicationCopiedBytesBaseline = 0;
		uint64_t m_ModelUseCount = 0;
		uint64_t m_MeshUseCount = 0;
		uint64_t m_TextureUseCount = 0;
		uint32_t m_PinnedProtectionFrames = 0;
		TaskHandle m_StaleTextureReloadTask{};
		TaskHandle m_ReplacementTextureReloadTask{};
		float m_ElapsedSeconds = 0.0f;
		Phase m_Phase = Phase::Loading;
		bool m_Passed = false;
		std::vector<std::string> m_Errors;
	};

	AssetResidencyLabSession::AssetResidencyLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
	{}

	void AssetResidencyLabSession::OnEnter() noexcept
	{
		m_State = std::make_unique<State>();
		std::string coordinatorError;
		if (!ValidateTextureDerivedDataCoordinatorContract(coordinatorError))
		{
			Fail(std::move(coordinatorError));
			return;
		}
		AssetManager& assetManager = *m_Services.m_AssetManager;
		m_State->m_PublicationCopiedBytesBaseline = BuildAssetSnapshot(assetManager)
			.m_ResourcePublicationQueue.m_SourceBytesCopiedToUpload;
		m_State->m_OriginalResidencyConfig = assetManager.GetResidencyConfig();
		assetManager.SetResidencyConfig({
			.m_EnableAutomaticEviction = false,
			.m_HighWatermarkBytes = 1,
			.m_LowWatermarkBytes = 0,
			.m_MinUnusedFrames = 0,
			.m_MaxEvictionsPerFrame = 16,
		});
		m_State->m_Request = GetAssetOwnerScope().LoadModelAsync(
			"Assets/Models/NormalTangentTest/NormalTangentTest.gltf",
			TaskPriority::Normal);
		if (!m_State->m_Request.IsValid())
		{
			Fail("AssetManager rejected the residency verification model.");
		}
	}

	void AssetResidencyLabSession::OnExit() noexcept
	{
		if (m_State)
		{
			m_Services.m_AssetManager->SetResidencyConfig(
				m_State->m_OriginalResidencyConfig);
		}
		ResetAssetInterests();
		m_State.reset();
	}

	void AssetResidencyLabSession::Update(float deltaTime) noexcept
	{
		GetCamera().Update();
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}
		m_State->m_ElapsedSeconds += deltaTime;
		if (m_State->m_ElapsedSeconds > 120.0f)
		{
			Fail("Asset residency verification timed out.");
			return;
		}

		AssetManager& assetManager = *m_Services.m_AssetManager;
		switch (m_State->m_Phase)
		{
		case State::Phase::Loading:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			if (!model || model->m_ContentGeneration != m_State->m_Request.m_Generation)
			{
				break;
			}
			if (model->m_State == AssetState::Failed || model->m_State == AssetState::Cancelled)
			{
				Fail("The residency verification model did not become Ready.");
				return;
			}
			if (model->m_State != AssetState::Ready)
			{
				break;
			}
			if (model->m_ContentState != AssetContentState::Ready ||
				model->m_ResidencyState != AssetResidencyState::Resident ||
				model->m_ResidencyEpoch == 0 || model->m_MeshInstance.empty())
			{
				Fail("The Ready model has invalid content or residency metadata.");
				return;
			}

			m_State->m_MeshId = model->m_MeshInstance.front().m_MeshId;
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const Material* material = assetManager.GetMaterial(
				model->m_MeshInstance.front().m_MaterialId);
			if (!mesh || !material || mesh->m_ResidencyState != AssetResidencyState::Resident)
			{
				Fail("The verification model has no resident mesh dependency.");
				return;
			}
			for (TextureID textureId : std::array{
				material->m_BaseColorBinding.m_TextureId,
				material->m_MetallicRoughnessBinding.m_TextureId,
				material->m_NormalBinding.m_TextureId,
				material->m_OcclusionBinding.m_TextureId,
				material->m_EmissiveBinding.m_TextureId })
			{
				if (textureId.IsValid() && !IsReservedTextureId(textureId))
				{
					m_State->m_TextureId = textureId;
					break;
				}
			}
			const AssetSnapshot dependencySnapshot = BuildAssetSnapshot(assetManager);
			const AssetStreamingQueueStatistics& publication =
				dependencySnapshot.m_ResourcePublicationQueue;
			if (publication.m_PendingCount != 0 || publication.m_PendingSourceBytes != 0)
			{
				break;
			}
			if (publication.m_SourceBytesCopiedToUpload !=
				m_State->m_PublicationCopiedBytesBaseline)
			{
				Fail(
					"Model publication created an independent CPU upload payload instead of borrowing immutable artifact data.");
				return;
			}
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				dependencySnapshot,
				m_State->m_TextureId);
			if (!texture || texture->m_ResidencyState != AssetResidencyState::Resident)
			{
				Fail("The verification model has no resident texture dependency.");
				return;
			}

			const AssetSnapshot::Model* dependencyModel = FindModelSnapshot(
				dependencySnapshot,
				m_State->m_Request.m_ModelId);
			if (!dependencyModel || !dependencyModel->m_HasDependencyState ||
				dependencyModel->m_DependencyCount == 0 ||
				dependencyModel->m_ReadyDependencyCount != dependencyModel->m_DependencyCount ||
				dependencyModel->m_PendingDependencyCount != 0 ||
				dependencyModel->m_FailedDependencyCount != 0 ||
				dependencyModel->m_CancelledDependencyCount != 0 ||
				dependencySnapshot.m_TrackedModelDependencyCount == 0 ||
				dependencySnapshot.m_ReverseDependencyEdgeCount <
					dependencyModel->m_DependencyCount ||
				dependencySnapshot.m_DependencyValidationCount == 0 ||
				dependencySnapshot.m_DependencyValidationMismatchCount != 0)
			{
				Fail("The model dependency graph did not converge with traversal-based readiness.");
				return;
			}
			if (!dependencyModel->m_ImportArtifactContentDigest.IsValid() ||
				!dependencyModel->m_IsImportArtifactCached ||
				dependencySnapshot.m_ModelImportArtifactCachedEntryCount == 0 ||
				dependencySnapshot.m_ModelImportArtifactAdmissionCount == 0)
			{
				Fail("The immutable model import artifact was not admitted to the CPU cache.");
				return;
			}

			if (!assetManager.SetModelResidencyPolicy(
				m_State->m_Request.m_ModelId,
				AssetResidencyPolicy::Pinned) ||
				!assetManager.SetModelResidencyPolicy(
					m_State->m_Request.m_ModelId,
					AssetResidencyPolicy::Cacheable) ||
				!assetManager.SetMeshResidencyPolicy(
					m_State->m_MeshId,
					AssetResidencyPolicy::Pinned) ||
				!assetManager.SetMeshResidencyPolicy(
					m_State->m_MeshId,
					AssetResidencyPolicy::Cacheable) ||
				!assetManager.SetTextureResidencyPolicy(
					m_State->m_TextureId,
					AssetResidencyPolicy::Pinned) ||
				!assetManager.SetTextureResidencyPolicy(
					m_State->m_TextureId,
					AssetResidencyPolicy::Cacheable))
			{
				Fail("A cacheable asset rejected a valid residency policy transition.");
				return;
			}
			if (!assetManager.SetModelResidencyPolicy(
				m_State->m_Request.m_ModelId,
				AssetResidencyPolicy::Pinned))
			{
				Fail("The verification model could not be pinned for eviction protection.");
				return;
			}
			const TextureID reservedTexture = ToTextureId(ReservedTextureIDIndex::BaseColorWhite);
			if (assetManager.SetTextureResidencyPolicy(
				reservedTexture,
				AssetResidencyPolicy::Cacheable))
			{
				Fail("A reserved texture accepted a cacheable residency policy.");
				return;
			}
			const AssetSnapshot reservedSnapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* pinnedTexture = FindTextureSnapshot(
				reservedSnapshot,
				reservedTexture);
			if (!pinnedTexture ||
				pinnedTexture->m_ResidencyPolicy != AssetResidencyPolicy::Pinned)
			{
				Fail("A reserved texture accepted a cacheable residency policy.");
				return;
			}

			m_State->m_ModelUseCount = model->m_UseCount;
			m_State->m_MeshUseCount = mesh->m_UseCount;
			m_State->m_TextureUseCount = texture->m_UseCount;
			m_State->m_ModelGeneration = model->m_ContentGeneration;
			m_State->m_MeshGeneration = mesh->m_ContentGeneration;
			m_State->m_TextureGeneration = texture->m_ContentGeneration;
			m_State->m_TextureImportSettings = texture->m_ImportSettings;
			m_State->m_MeshResidencyEpoch = mesh->m_ResidencyEpoch;
			m_State->m_TextureResidencyEpoch = texture->m_ResidencyEpoch;
			const AssetResidencyStatistics residency =
				assetManager.GetResidencyStatistics();
			m_State->m_EvictionCountBaseline = residency.m_EvictionCount;
			m_State->m_EvictionCancellationCountBaseline =
				residency.m_EvictionCancellationCount;
			m_State->m_ReloadRequestCountBaseline = residency.m_ReloadRequestCount;
			m_State->m_OperationCountBaseline = residency.m_OperationCount;
			m_State->m_StaleCompletionCountBaseline = residency.m_StaleCompletionCount;
			m_State->m_AcceptedStateEventCountBaseline =
				residency.m_AcceptedStateEventCount;
			m_State->m_CompletedStateEventCountBaseline =
				residency.m_CompletedStateEventCount;
			m_State->m_ModelImportArtifactCacheHitCountBaseline =
				dependencySnapshot.m_ModelImportArtifactCacheHitCount;
			m_State->m_Phase = State::Phase::MarkUsage;
			break;
		}

		case State::Phase::MarkUsage:
		{
			assetManager.MarkModelUsed(m_State->m_Request.m_ModelId);
			assetManager.MarkModelUsed(m_State->m_Request.m_ModelId);
			assetManager.MarkMeshUsed(m_State->m_MeshId);
			assetManager.MarkMeshUsed(m_State->m_MeshId);
			assetManager.MarkTextureUsed(m_State->m_TextureId);
			assetManager.MarkTextureUsed(m_State->m_TextureId);

			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Model* model = FindModelSnapshot(
				snapshot,
				m_State->m_Request.m_ModelId);
			const AssetSnapshot::Mesh* mesh = FindMeshSnapshot(snapshot, m_State->m_MeshId);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture ||
				model->m_UseCount != m_State->m_ModelUseCount + 1 ||
				mesh->m_UseCount != m_State->m_MeshUseCount + 1 ||
				texture->m_UseCount != m_State->m_TextureUseCount + 1 ||
				model->m_LastUsedFrame != snapshot.m_AssetUsageFrame ||
				mesh->m_LastUsedFrame != snapshot.m_AssetUsageFrame ||
				texture->m_LastUsedFrame != snapshot.m_AssetUsageFrame)
			{
				Fail("Per-frame asset usage tracking did not deduplicate repeated marks.");
				return;
			}
			ResetAssetInterests();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::WaitForPinnedProtection;
			break;
		}

		case State::Phase::WaitForPinnedProtection:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture ||
				model->m_ContentGeneration != m_State->m_ModelGeneration ||
				mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("Pinned residency protection replaced a stable asset entry.");
				return;
			}
			if (model->m_ResidencyPolicy != AssetResidencyPolicy::Pinned ||
				model->m_State != AssetState::Ready ||
				mesh->m_State != AssetState::Ready ||
				texture->m_State != AssetState::Ready ||
				mesh->m_ResidencyState != AssetResidencyState::Resident ||
				texture->m_ResidencyState != AssetResidencyState::Resident)
			{
				Fail("A pinned model did not protect its resident dependencies from eviction.");
				return;
			}
			const AssetResidencyStatistics residency = assetManager.GetResidencyStatistics();
			if (residency.m_EvictionCount != m_State->m_EvictionCountBaseline ||
				residency.m_PendingEvictionCount != 0)
			{
				Fail("Automatic eviction scheduled or finalized work for a pinned model.");
				return;
			}
			if (++m_State->m_PinnedProtectionFrames < 8)
			{
				break;
			}
			if (!assetManager.SetModelResidencyPolicy(
				m_State->m_Request.m_ModelId,
				AssetResidencyPolicy::Cacheable))
			{
				Fail("The verification model could not return to cacheable residency.");
				return;
			}
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::ReleaseOwner;
			break;
		}

		case State::Phase::ReleaseOwner:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Model* model = FindModelSnapshot(
				snapshot,
				m_State->m_Request.m_ModelId);
			const AssetSnapshot::Mesh* mesh = FindMeshSnapshot(snapshot, m_State->m_MeshId);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture ||
				!model->m_IsEvictionCandidate ||
				!mesh->m_IsEvictionCandidate ||
				!texture->m_IsEvictionCandidate ||
				!model->m_HasDependencyState ||
				snapshot.m_DependencyValidationMismatchCount != 0)
			{
				Fail("Unowned cacheable resident assets were not classified as eviction candidates.");
				return;
			}
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::WaitForEvictionStart;
			break;
		}

		case State::Phase::WaitForEvictionStart:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture ||
				model->m_ContentGeneration != m_State->m_ModelGeneration ||
				mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("Eviction start replaced a stable asset entry.");
				return;
			}
			if (mesh->m_ResidencyState != AssetResidencyState::Evicting ||
				texture->m_ResidencyState != AssetResidencyState::Evicting)
			{
				break;
			}
			const AssetManager::ModelLoadRequest revived =
				GetAssetOwnerScope().LoadModelAsync(
					"Assets/Models/NormalTangentTest/NormalTangentTest.gltf",
					TaskPriority::Critical);
			if (!revived.IsValid() ||
				revived.m_ModelId != m_State->m_Request.m_ModelId ||
				revived.m_Generation != m_State->m_ModelGeneration)
			{
				Fail("Reacquiring an evicting model did not preserve its content version.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForEvictionCancellation;
			break;
		}

		case State::Phase::WaitForEvictionCancellation:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture)
			{
				Fail("Eviction cancellation removed a stable asset entry.");
				return;
			}
			if (model->m_State == AssetState::Failed || model->m_State == AssetState::Cancelled ||
				mesh->m_State == AssetState::Failed || mesh->m_State == AssetState::Cancelled ||
				texture->m_State == AssetState::Failed || texture->m_State == AssetState::Cancelled)
			{
				Fail("Reacquiring an evicting model produced a terminal asset state.");
				return;
			}
			if (model->m_State != AssetState::Ready ||
				mesh->m_State != AssetState::Ready ||
				texture->m_State != AssetState::Ready)
			{
				break;
			}
			const AssetResidencyStatistics residency = assetManager.GetResidencyStatistics();
			if (residency.m_EvictionCancellationCount <
				m_State->m_EvictionCancellationCountBaseline + 2 ||
				residency.m_EvictionCount != m_State->m_EvictionCountBaseline ||
				residency.m_OperationCount < m_State->m_OperationCountBaseline + 4 ||
				residency.m_StaleCompletionCount <
					m_State->m_StaleCompletionCountBaseline + 2 ||
				mesh->m_ResidencyState != AssetResidencyState::Resident ||
				texture->m_ResidencyState != AssetResidencyState::Resident ||
				!mesh->m_IsUploaded || !texture->m_IsUploaded)
			{
				Fail("Reacquiring interest did not cancel pending eviction before resource release.");
				return;
			}
			if (snapshot.m_DependencyValidationMismatchCount != 0)
			{
				Fail("Dependency tracking diverged while eviction was cancelled.");
				return;
			}
			ResetAssetInterests();
			m_State->m_Phase = State::Phase::WaitForRelease;
			break;
		}

		case State::Phase::WaitForRelease:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture ||
				model->m_ContentGeneration != m_State->m_ModelGeneration ||
				mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("Residency release replaced a stable asset entry.");
				return;
			}
			if (mesh->m_ResidencyState == AssetResidencyState::Evicting ||
				texture->m_ResidencyState == AssetResidencyState::Evicting)
			{
				break;
			}
			if (mesh->m_State != AssetState::CpuReady ||
				texture->m_State != AssetState::CpuReady ||
				mesh->m_ContentState != AssetContentState::Ready ||
				texture->m_ContentState != AssetContentState::Ready ||
				mesh->m_ResidencyState != AssetResidencyState::NonResident ||
				texture->m_ResidencyState != AssetResidencyState::NonResident ||
				mesh->m_IsUploaded || texture->m_IsUploaded ||
				mesh->m_VertexBuffer || mesh->m_IndexBuffer ||
				texture->m_Texture.IsValid() || texture->m_HasSrv)
			{
				Fail("Released assets did not preserve content while dropping GPU residency.");
				return;
			}
			const AssetResidencyStatistics released =
				assetManager.GetResidencyStatistics();
			if (released.m_EvictionCount < m_State->m_EvictionCountBaseline + 2)
			{
				Fail("The residency controller did not finalize mesh and texture releases.");
				return;
			}
			const TextureArtifactCacheStatistics artifactCache =
				assetManager.GetTextureArtifactCacheStatistics();
			if (!texture->m_IsCpuArtifactCached ||
				!texture->m_ArtifactContentDigest.IsValid() ||
				artifactCache.m_CachedEntryCount == 0 ||
				artifactCache.m_CachedBytes == 0)
			{
				Fail("The decoded texture was not retained by the CPU artifact cache.");
				return;
			}

			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			assetManager.SetResidencyConfig(config);
			m_State->m_TextureArtifactCacheHitCountBaseline = artifactCache.m_HitCount;
			const AssetManager::TextureLoadRequest cacheReload =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Critical);
			const TextureArtifactCacheStatistics cacheReloadStatistics =
				assetManager.GetTextureArtifactCacheStatistics();
			if (!cacheReload.IsValid() || cacheReload.m_Task.IsValid() ||
				cacheReload.m_TextureId != m_State->m_TextureId ||
				cacheReload.m_Generation != m_State->m_TextureGeneration ||
				cacheReloadStatistics.m_HitCount !=
					m_State->m_TextureArtifactCacheHitCountBaseline + 1)
			{
				Fail("The texture residency reload did not use its cached CPU artifact.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForTextureArtifactCacheReload;
			break;
		}

		case State::Phase::WaitForTextureArtifactCacheReload:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!texture || texture->m_State == AssetState::Failed ||
				texture->m_State == AssetState::Cancelled)
			{
				Fail("The CPU artifact cache texture reload failed.");
				return;
			}
			if (texture->m_State != AssetState::Ready)
			{
				break;
			}
			const TextureArtifactCacheStatistics artifactCache =
				assetManager.GetTextureArtifactCacheStatistics();
			if (texture->m_ResidencyState != AssetResidencyState::Resident ||
				!texture->m_IsUploaded || !texture->m_Texture.IsValid() ||
				!texture->m_HasSrv || !texture->m_IsCpuArtifactCached ||
				artifactCache.m_HitCount !=
					m_State->m_TextureArtifactCacheHitCountBaseline + 1)
			{
				Fail("The cached CPU artifact did not restore texture GPU residency.");
				return;
			}

			ResetAssetInterests();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::WaitForTextureArtifactCacheRelease;
			break;
		}

		case State::Phase::WaitForTextureArtifactCacheRelease:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!texture || texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("The cache-hit reload replaced its stable texture entry.");
				return;
			}
			if (texture->m_ResidencyState == AssetResidencyState::Evicting)
			{
				break;
			}
			if (texture->m_State != AssetState::CpuReady ||
				texture->m_ResidencyState != AssetResidencyState::NonResident ||
				texture->m_IsUploaded || texture->m_Texture.IsValid() ||
				texture->m_HasSrv)
			{
				Fail("The cache-hit texture could not return to non-resident state.");
				return;
			}

			assetManager.ClearTextureArtifactCache();
			if (assetManager.GetTextureArtifactCacheStatistics().m_CachedEntryCount != 0)
			{
				Fail("Clearing the texture CPU artifact cache left cached entries behind.");
				return;
			}
			assetManager.ClearTextureDerivedDataCache();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			assetManager.SetResidencyConfig(config);
			m_State->m_TextureDdcWriteCountBaseline =
				assetManager.GetTextureDerivedDataStatistics().m_WriteCount;
			const AssetManager::TextureLoadRequest sourceBuild =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Critical);
			if (!sourceBuild.IsValid() || !sourceBuild.m_Task.IsValid() ||
				sourceBuild.m_TextureId != m_State->m_TextureId ||
				sourceBuild.m_Generation != m_State->m_TextureGeneration)
			{
				Fail("The texture DDC source build could not start.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForTextureDdcBuild;
			break;
		}

		case State::Phase::WaitForTextureDdcBuild:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!texture || texture->m_State == AssetState::Failed || texture->m_State == AssetState::Cancelled)
			{
				Fail("The texture DDC source build failed.");
				return;
			}
			if (texture->m_State != AssetState::Ready) break;
			const LocalDerivedDataStoreStatistics ddc = assetManager.GetTextureDerivedDataStatistics();
			if (!texture->m_DerivedDataKey.IsValid() || !texture->m_SourceDigest.IsValid() ||
				!texture->m_IsDerivedDataCached ||
				ddc.m_WriteCount < m_State->m_TextureDdcWriteCountBaseline + 1)
			{
				Fail("The source-built texture was not published to the local DDC.");
				return;
			}
			ResetAssetInterests();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::WaitForTextureDdcBuildRelease;
			break;
		}

		case State::Phase::WaitForTextureDdcBuildRelease:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!texture) { Fail("The DDC source-built texture entry disappeared."); return; }
			if (texture->m_ResidencyState == AssetResidencyState::Evicting) break;
			if (texture->m_State != AssetState::CpuReady ||
				texture->m_ResidencyState != AssetResidencyState::NonResident)
			{
				Fail("The DDC source-built texture did not become non-resident.");
				return;
			}
			assetManager.ClearTextureArtifactCache();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			assetManager.SetResidencyConfig(config);
			m_State->m_TextureDdcHitCountBaseline =
				assetManager.GetTextureDerivedDataStatistics().m_HitCount;
			const AssetManager::TextureLoadRequest ddcReload =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Critical);
			if (!ddcReload.IsValid() || !ddcReload.m_Task.IsValid())
			{
				Fail("The texture DDC reload could not start.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForTextureDdcReload;
			break;
		}

		case State::Phase::WaitForTextureDdcReload:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!texture || texture->m_State == AssetState::Failed || texture->m_State == AssetState::Cancelled)
			{
				Fail("The texture local DDC reload failed.");
				return;
			}
			if (texture->m_State != AssetState::Ready) break;
			if (assetManager.GetTextureDerivedDataStatistics().m_HitCount <
				m_State->m_TextureDdcHitCountBaseline + 1)
			{
				Fail("The texture reload did not record a local DDC hit.");
				return;
			}
			ResetAssetInterests();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = true;
			assetManager.SetResidencyConfig(config);
			m_State->m_Phase = State::Phase::WaitForTextureDdcReloadRelease;
			break;
		}

		case State::Phase::WaitForTextureDdcReloadRelease:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!texture) { Fail("The DDC-hit texture entry disappeared."); return; }
			if (texture->m_ResidencyState == AssetResidencyState::Evicting) break;
			if (texture->m_State != AssetState::CpuReady ||
				texture->m_ResidencyState != AssetResidencyState::NonResident)
			{
				Fail("The DDC-hit texture did not become non-resident.");
				return;
			}
			assetManager.ClearTextureArtifactCache();
			assetManager.ClearTextureDerivedDataCache();
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_EnableAutomaticEviction = false;
			assetManager.SetResidencyConfig(config);
			const AssetManager::TextureLoadRequest staleReload =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Background);
			if (!staleReload.IsValid() || !staleReload.m_Task.IsValid() ||
				staleReload.m_TextureId != m_State->m_TextureId ||
				staleReload.m_Generation != m_State->m_TextureGeneration)
			{
				Fail("The texture reload replacement probe could not start its first task.");
				return;
			}
			m_State->m_StaleTextureReloadTask = staleReload.m_Task;
			m_State->m_Phase = State::Phase::WaitForTextureReloadRunning;
			break;
		}

		case State::Phase::WaitForTextureReloadRunning:
		{
			const TaskSystemStatistics tasks = m_Services.m_TaskSystem->GetStatistics();
			const auto activity = std::ranges::find(
				tasks.m_ActiveTasks,
				m_State->m_StaleTextureReloadTask,
				&TaskActivity::m_Handle);
			if (activity == tasks.m_ActiveTasks.end() ||
				activity->m_Status != TaskStatus::Running ||
				activity->m_ExecutionMilliseconds < 25.0)
			{
				break;
			}

			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!texture || texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("The texture reload replacement probe lost its stable texture entry.");
				return;
			}
			ResetAssetInterests();
			const AssetManager::TextureLoadRequest replacementReload =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Critical);
			if (!replacementReload.IsValid() || !replacementReload.m_Task.IsValid() ||
				replacementReload.m_TextureId != m_State->m_TextureId ||
				replacementReload.m_Generation != m_State->m_TextureGeneration ||
				replacementReload.m_Task == m_State->m_StaleTextureReloadTask)
			{
				Fail("Cancelling a running texture reload did not create a replacement task.");
				return;
			}
			m_State->m_ReplacementTextureReloadTask = replacementReload.m_Task;
			m_State->m_Phase = State::Phase::WaitForTextureReloadReplacement;
			break;
		}

		case State::Phase::WaitForTextureReloadReplacement:
		{
			const TaskSystemStatistics tasks = m_Services.m_TaskSystem->GetStatistics();
			const bool staleCompletionDelivered = std::ranges::any_of(
				tasks.m_RecentTasks,
				[this](const TaskCompletionInfo& completion) noexcept
				{
					return completion.m_Handle == m_State->m_StaleTextureReloadTask;
				});
			if (!staleCompletionDelivered)
			{
				break;
			}
			const auto replacementCompletion = std::ranges::find(
				tasks.m_RecentTasks,
				m_State->m_ReplacementTextureReloadTask,
				&TaskCompletionInfo::m_Handle);
			if (replacementCompletion != tasks.m_RecentTasks.end())
			{
				if (replacementCompletion->m_Status != TaskStatus::Succeeded)
				{
					Fail("The replacement texture decode did not complete successfully.");
					return;
				}
				m_State->m_Phase = State::Phase::WaitForTextureReloadCompletion;
				break;
			}

			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!texture)
			{
				Fail("The texture reload replacement probe lost its texture entry.");
				return;
			}
			const AssetManager::TextureLoadRequest trackedReplacement =
				GetAssetOwnerScope().LoadTextureAsync(
					texture->m_SourcePath,
					texture->m_ImportSettings.m_Semantic,
					TaskPriority::Critical);
			if (!trackedReplacement.m_Task.IsValid() ||
				trackedReplacement.m_Task != m_State->m_ReplacementTextureReloadTask)
			{
				Fail("A stale texture completion removed the replacement task record.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForTextureReloadCompletion;
			break;
		}

		case State::Phase::WaitForTextureReloadCompletion:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!texture || texture->m_State == AssetState::Failed ||
				texture->m_State == AssetState::Cancelled)
			{
				Fail("The replacement texture residency reload failed.");
				return;
			}
			if (texture->m_State != AssetState::Ready)
			{
				break;
			}

			const AssetManager::ModelLoadRequest reloaded =
				GetAssetOwnerScope().LoadModelAsync(
					"Assets/Models/NormalTangentTest/NormalTangentTest.gltf",
					TaskPriority::Normal);
			if (!reloaded.IsValid() ||
				reloaded.m_ModelId != m_State->m_Request.m_ModelId ||
				reloaded.m_Generation != m_State->m_ModelGeneration)
			{
				Fail("Reload did not preserve the model ID and content generation.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForReload;
			break;
		}

		case State::Phase::WaitForReload:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(
				snapshot,
				m_State->m_TextureId);
			if (!model || !mesh || !texture)
			{
				Fail("A stable asset entry disappeared during residency reload.");
				return;
			}
			if (model->m_State == AssetState::Failed ||
				model->m_State == AssetState::Cancelled ||
				mesh->m_State == AssetState::Failed ||
				mesh->m_State == AssetState::Cancelled ||
				texture->m_State == AssetState::Failed ||
				texture->m_State == AssetState::Cancelled)
			{
				Fail("A source-backed residency reload failed.");
				return;
			}
			if (model->m_State != AssetState::Ready ||
				mesh->m_State != AssetState::Ready ||
				texture->m_State != AssetState::Ready)
			{
				break;
			}
			const TextureContentRef textureContent =
				assetManager.GetTextureContentRef(m_State->m_TextureId);
			if (!textureContent.IsValid() ||
				textureContent.m_Generation != m_State->m_TextureGeneration ||
				!assetManager.GetResidentTextureResource(textureContent))
			{
				Fail("The resident texture render view was unavailable for current content.");
				return;
			}
			TextureContentRef staleTextureContent = textureContent;
			++staleTextureContent.m_Generation;
			if (assetManager.GetResidentTextureResource(staleTextureContent))
			{
				Fail("The resident texture render view accepted a stale content generation.");
				return;
			}
			const AssetResidencyStatistics reloaded =
				assetManager.GetResidencyStatistics();
			if (model->m_ContentGeneration != m_State->m_ModelGeneration ||
				mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration ||
				texture->m_ImportSettings != m_State->m_TextureImportSettings ||
				mesh->m_ResidencyEpoch <= m_State->m_MeshResidencyEpoch ||
				texture->m_ResidencyEpoch <= m_State->m_TextureResidencyEpoch ||
				mesh->m_ResidencyOperationSerial != 0 ||
				texture->m_ResidencyOperationSerial != 0 ||
				!mesh->m_IsUploaded || !texture->m_IsUploaded ||
				reloaded.m_ReloadRequestCount <= m_State->m_ReloadRequestCountBaseline ||
				reloaded.m_ReloadingAssetCount != 0 ||
				reloaded.m_AcceptedStateEventCount <=
					m_State->m_AcceptedStateEventCountBaseline ||
				reloaded.m_CompletedStateEventCount <
					m_State->m_CompletedStateEventCountBaseline + 6 ||
				reloaded.m_AcceptedStateEventCount <
					reloaded.m_CompletedStateEventCount)
			{
				Fail(
					"Reload did not restore residency through validated state-operation events.");
				return;
			}
			if (snapshot.m_DependencyValidationMismatchCount != 0)
			{
				Fail("Dependency tracking diverged during residency reload.");
				return;
			}
			const AssetSnapshot::Model* reloadedModel = FindModelSnapshot(
				snapshot,
				m_State->m_Request.m_ModelId);
			if (!reloadedModel || !reloadedModel->m_IsImportArtifactCached ||
				!reloadedModel->m_ImportArtifactContentDigest.IsValid() ||
				snapshot.m_ModelImportArtifactCacheHitCount <=
					m_State->m_ModelImportArtifactCacheHitCountBaseline)
			{
				Fail("Mesh residency reload did not reuse the immutable model import artifact.");
				return;
			}
			Complete();
			break;
		}

		case State::Phase::Completed:
			break;
		}
	}

	void AssetResidencyLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Asset Residency Verification";
		if (!m_State)
		{
			return;
		}
		diagnostics.m_Metrics = {
			{ .m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds) },
			{ .m_Name = "Model", .m_Value = std::to_string(m_State->m_Request.m_ModelId.Value()) },
			{ .m_Name = "Mesh", .m_Value = std::to_string(m_State->m_MeshId.Value()) },
			{ .m_Name = "Texture", .m_Value = std::to_string(m_State->m_TextureId.Value()) },
		};
		diagnostics.m_Checks.push_back({
			.m_Name = "Residency invariants",
			.m_Status = m_State->m_Phase != State::Phase::Completed ?
				LabDiagnosticCheckStatus::Pending :
				m_State->m_Passed ? LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Failed,
			.m_Detail = m_State->m_Phase != State::Phase::Completed ?
				"Verification is running." :
				m_State->m_Passed ? "All residency invariants passed." :
					std::format("{} invariant errors.", m_State->m_Errors.size()),
		});
		for (const std::string& error : m_State->m_Errors)
		{
			diagnostics.m_Checks.push_back({
				.m_Name = "Invariant",
				.m_Status = LabDiagnosticCheckStatus::Failed,
				.m_Detail = error,
			});
		}
	}

	void AssetResidencyLabSession::Fail(std::string error) noexcept
	{
		if (!m_State || m_State->m_Phase == State::Phase::Completed)
		{
			return;
		}
		m_State->m_Errors.push_back(std::move(error));
		m_State->m_Passed = false;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_ERROR("ASSET RESIDENCY ACCEPTANCE FAIL: {}", m_State->m_Errors.back());
	}

	void AssetResidencyLabSession::Complete() noexcept
	{
		GGLAB_ASSERT(m_State);
		m_State->m_Passed = true;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_INFO(
			"ASSET RESIDENCY ACCEPTANCE PASS: lifecycle, dependency, policy, usage, publication source ownership accounting, pinned protection, eviction cancellation, release, immutable model import artifact cache hit, texture CPU artifact cache reload, local DDC build/hit/fallback, shared artifact build/wait/cancel fan-out, validated state-operation events, texture reload replacement, generation-safe render views, and stable-ID reload invariants passed in {:.2f} s.",
			m_State->m_ElapsedSeconds);
	}

	LabId AssetResidencyLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.asset_residency");
	}

	LabDescriptor AssetResidencyLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Asset Residency Lab",
			.m_Category = "Systems",
			.m_Description = "Validates logical residency, CPU artifact/DDC reload, fence-safe release, and stable-ID source reload invariants.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> AssetResidencyLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AssetResidencyLabSession>(createInfo);
	}
}
