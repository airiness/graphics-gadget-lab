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
			Completed,
		};

		AssetManager::ModelLoadRequest m_Request{};
		MeshID m_MeshId{};
		TextureID m_TextureId{};
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
				!texture->m_IsEvictionCandidate)
			{
				Fail("Unowned cacheable resident assets were not classified as eviction candidates.");
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
			"ASSET RESIDENCY ACCEPTANCE PASS: content, residency, policy, usage, and candidate invariants passed in {:.2f} s.",
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
			.m_Description = "Validates asset content, logical residency, policy, usage, and eviction-candidate invariants.",
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
