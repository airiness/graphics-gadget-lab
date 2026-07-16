#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/AssetResidencyLabSession.h"
#include "Diagnostics/Builders/AssetSnapshotBuilder.h"
#include "Diagnostics/Snapshots/AssetSnapshot.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/AssetManager.h"
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
	}

	struct AssetResidencyLabSession::State
	{
		enum class Phase : uint8_t
		{
			Loading,
			MarkUsage,
			ReleaseOwner,
			WaitForRelease,
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
		uint64_t m_MeshResidencyEpoch = 0;
		uint64_t m_TextureResidencyEpoch = 0;
		uint64_t m_EvictionCountBaseline = 0;
		uint64_t m_ReloadRequestCountBaseline = 0;
		uint64_t m_ModelUseCount = 0;
		uint64_t m_MeshUseCount = 0;
		uint64_t m_TextureUseCount = 0;
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
		AssetManager& assetManager = *m_Services.m_AssetManager;
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
			const Texture* texture = assetManager.GetTexture(m_State->m_TextureId);
			if (!texture || texture->m_ResidencyState != AssetResidencyState::Resident)
			{
				Fail("The verification model has no resident texture dependency.");
				return;
			}

			const AssetSnapshot dependencySnapshot = BuildAssetSnapshot(assetManager);
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
			const TextureID reservedTexture = ToTextureId(ReservedTextureIDIndex::BaseColorWhite);
			const Texture* pinnedTexture = assetManager.GetTexture(reservedTexture);
			if (!pinnedTexture ||
				assetManager.SetTextureResidencyPolicy(
				reservedTexture,
				AssetResidencyPolicy::Cacheable) ||
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
			m_State->m_MeshResidencyEpoch = mesh->m_ResidencyEpoch;
			m_State->m_TextureResidencyEpoch = texture->m_ResidencyEpoch;
			const AssetResidencyStatistics residency =
				assetManager.GetResidencyStatistics();
			m_State->m_EvictionCountBaseline = residency.m_EvictionCount;
			m_State->m_ReloadRequestCountBaseline = residency.m_ReloadRequestCount;
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
			m_State->m_Phase = State::Phase::WaitForRelease;
			break;
		}

		case State::Phase::WaitForRelease:
		{
			const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const Texture* texture = assetManager.GetTexture(m_State->m_TextureId);
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
				texture->m_Texture.IsValid() || texture->m_Srv.IsValid())
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
			const Texture* texture = assetManager.GetTexture(m_State->m_TextureId);
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
			const AssetResidencyStatistics reloaded =
				assetManager.GetResidencyStatistics();
			if (model->m_ContentGeneration != m_State->m_ModelGeneration ||
				mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration ||
				mesh->m_ResidencyEpoch <= m_State->m_MeshResidencyEpoch ||
				texture->m_ResidencyEpoch <= m_State->m_TextureResidencyEpoch ||
				!mesh->m_IsUploaded || !texture->m_IsUploaded ||
				reloaded.m_ReloadRequestCount <= m_State->m_ReloadRequestCountBaseline ||
				reloaded.m_ReloadingAssetCount != 0)
			{
				Fail("Reload did not restore residency on the original asset identities.");
				return;
			}
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			if (snapshot.m_DependencyValidationMismatchCount != 0)
			{
				Fail("Dependency tracking diverged during residency reload.");
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
			"ASSET RESIDENCY ACCEPTANCE PASS: lifecycle, dependency, policy, usage, eviction, and stable-ID reload invariants passed in {:.2f} s.",
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
			.m_Description = "Validates logical residency, fence-safe release, and stable-ID source reload invariants.",
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
