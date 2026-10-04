#include "Application/Lab/Sessions/AssetResidencyLabSession.h"
#include "AppRuntimeLog.h"
#include "GGLabRuntime/Diagnostics/AssetSnapshotRead.h"
#include "GGLabRuntime/Diagnostics/Snapshots/AssetSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Asset/ReservedTexture.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"

#include <algorithm>
#include <array>
#include <format>

namespace gglab
{
	namespace
	{
		constexpr const char* ResidencyModelPath = "Assets/Models/NormalTangentTest/NormalTangentTest.gltf";

		[[nodiscard]] const AssetSnapshot::Texture* FindTextureSnapshot(
			const AssetSnapshot& snapshot, TextureID id) noexcept
		{
			const auto iterator =
				std::ranges::find(snapshot.m_Textures, id, &AssetSnapshot::Texture::m_Id);
			return iterator != snapshot.m_Textures.end() ? &*iterator : nullptr;
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
	}

	struct AssetResidencyLabSession::State
	{
		enum class Phase : uint8_t
		{
			Loading,
			WaitForRelease,
			WaitForReload,
			WaitForRetirement,
			Completed,
		};

		AssetOwnerScope m_Owner;
		AssetManager::ModelLoadRequest m_Request{};
		AssetResidencyConfig m_OriginalResidencyConfig{};
		MeshID m_MeshId{};
		MaterialID m_MaterialId{};
		TextureID m_TextureId{};
		uint64_t m_MeshGeneration = 0;
		uint64_t m_TextureGeneration = 0;
		uint64_t m_MeshResidencyEpoch = 0;
		uint64_t m_TextureResidencyEpoch = 0;
		float m_ElapsedSeconds = 0.0f;
		Phase m_Phase = Phase::Loading;
		bool m_Passed = false;
		std::vector<std::string> m_Errors;
	};

	AssetResidencyLabSession::AssetResidencyLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPlus())
	{
	}

	AssetResidencyLabSession::~AssetResidencyLabSession() = default;

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
			.m_RuntimeEntryRetentionFrames = 1'000'000,
			.m_MaxRuntimeRetirementsPerFrame = 64,
			});
		m_State->m_Owner = assetManager.CreateOwnerScope();
		m_State->m_Request = m_State->m_Owner.LoadModelAsync(ResidencyModelPath, TaskPriority::Normal);
		if (!m_State->m_Request.IsValid())
		{
			Fail("AssetManager rejected the residency verification model.");
		}
	}

	void AssetResidencyLabSession::OnExit() noexcept
	{
		if (m_State)
		{
			m_Services.m_AssetManager->SetResidencyConfig(m_State->m_OriginalResidencyConfig);
		}
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
		const auto setAutomaticEviction = [&assetManager](bool enabled) noexcept
			{
				AssetResidencyConfig config = assetManager.GetResidencyConfig();
				config.m_EnableAutomaticEviction = enabled;
				assetManager.SetResidencyConfig(config);
			};
		const Model* model = assetManager.GetModel(m_State->m_Request.m_ModelId);
		switch (m_State->m_Phase)
		{
		case State::Phase::Loading:
		{
			if (!model || model->m_ContentGeneration != m_State->m_Request.m_Generation ||
				(model->m_State != AssetState::Ready && model->m_State != AssetState::Failed &&
					model->m_State != AssetState::Cancelled))
			{
				break;
			}
			if (model->m_State != AssetState::Ready || model->m_MeshInstance.empty())
			{
				Fail("The residency verification model did not become Ready.");
				return;
			}
			m_State->m_MeshId = model->m_MeshInstance.front().m_MeshId;
			m_State->m_MaterialId = model->m_MeshInstance.front().m_MaterialId;
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const Material* material = assetManager.GetMaterial(m_State->m_MaterialId);
			m_State->m_TextureId = material ? FindFirstRuntimeTexture(*material) : TextureID{};
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			const TextureContentRef content = assetManager.GetTextureContentRef(m_State->m_TextureId);
			if (!mesh || !texture || !mesh->m_IsUploaded || !texture->m_IsUploaded ||
				!assetManager.GetResidentTextureResource(content))
			{
				Fail("The Ready model has no resident GPU mesh and texture view.");
				return;
			}
			m_State->m_MeshGeneration = mesh->m_ContentGeneration;
			m_State->m_TextureGeneration = texture->m_ContentGeneration;
			m_State->m_MeshResidencyEpoch = mesh->m_ResidencyEpoch;
			m_State->m_TextureResidencyEpoch = texture->m_ResidencyEpoch;
			m_State->m_Owner.Reset();
			setAutomaticEviction(true);
			m_State->m_Phase = State::Phase::WaitForRelease;
			break;
		}

		case State::Phase::WaitForRelease:
		{
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!mesh || !texture || mesh->m_ContentGeneration != m_State->m_MeshGeneration ||
				texture->m_ContentGeneration != m_State->m_TextureGeneration)
			{
				Fail("Residency release replaced a stable asset entry.");
				return;
			}
			if (mesh->m_ResidencyState != AssetResidencyState::NonResident ||
				texture->m_ResidencyState != AssetResidencyState::NonResident)
			{
				break;
			}
			if (mesh->m_IsUploaded || mesh->m_VertexBuffer || mesh->m_IndexBuffer ||
				texture->m_IsUploaded || texture->m_Texture.IsValid() || texture->m_HasSrv)
			{
				Fail("Fence-completed release kept GPU buffers, textures or views alive.");
				return;
			}
			setAutomaticEviction(false);
			const AssetManager::ModelLoadRequest reloaded =
				m_State->m_Owner.LoadModelAsync(ResidencyModelPath, TaskPriority::Normal);
			if (!reloaded.IsValid() || reloaded.m_ModelId != m_State->m_Request.m_ModelId ||
				reloaded.m_Generation != m_State->m_Request.m_Generation)
			{
				Fail("Reload did not preserve the model ID and content generation.");
				return;
			}
			m_State->m_Phase = State::Phase::WaitForReload;
			break;
		}

		case State::Phase::WaitForReload:
		{
			const Mesh* mesh = assetManager.GetMesh(m_State->m_MeshId);
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			const AssetSnapshot::Texture* texture = FindTextureSnapshot(snapshot, m_State->m_TextureId);
			if (!model || !mesh || !texture)
			{
				Fail("A stable asset entry disappeared during residency reload.");
				return;
			}
			if (model->m_State != AssetState::Ready || mesh->m_State != AssetState::Ready ||
				texture->m_State != AssetState::Ready)
			{
				if (model->m_State == AssetState::Failed || texture->m_State == AssetState::Failed)
				{
					Fail("A residency reload failed on the device.");
				}
				break;
			}
			const TextureContentRef content = assetManager.GetTextureContentRef(m_State->m_TextureId);
			TextureContentRef staleContent = content;
			++staleContent.m_Generation;
			if (!mesh->m_IsUploaded || !texture->m_IsUploaded ||
				mesh->m_ResidencyEpoch <= m_State->m_MeshResidencyEpoch ||
				texture->m_ResidencyEpoch <= m_State->m_TextureResidencyEpoch ||
				!assetManager.GetResidentTextureResource(content) ||
				assetManager.GetResidentTextureResource(staleContent))
			{
				Fail("Reload did not restore a generation-safe resident GPU texture view.");
				return;
			}
			AssetResidencyConfig config = assetManager.GetResidencyConfig();
			config.m_RuntimeEntryRetentionFrames = 0;
			assetManager.SetResidencyConfig(config);
			m_State->m_Owner = {};
			m_State->m_Phase = State::Phase::WaitForRetirement;
			break;
		}

		case State::Phase::WaitForRetirement:
		{
			const AssetSnapshot snapshot = BuildAssetSnapshot(assetManager);
			if (model || assetManager.GetMesh(m_State->m_MeshId) ||
				assetManager.GetMaterial(m_State->m_MaterialId) ||
				FindTextureSnapshot(snapshot, m_State->m_TextureId))
			{
				break;
			}
			const TextureContentRef retired{
				.m_Id = m_State->m_TextureId,
				.m_Generation = m_State->m_TextureGeneration,
			};
			if (assetManager.GetResidentTextureResource(retired))
			{
				Fail("A retired texture kept an addressable GPU view.");
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
		diagnostics.m_Title = "Asset Residency GPU Acceptance";
		if (!m_State)
		{
			return;
		}
		diagnostics.m_Metrics = {
			{.m_Name = "Elapsed", .m_Value = std::format("{:.2f} s", m_State->m_ElapsedSeconds)},
			{.m_Name = "Model", .m_Value = std::to_string(m_State->m_Request.m_ModelId.Value())},
			{.m_Name = "Mesh", .m_Value = std::to_string(m_State->m_MeshId.Value())},
			{.m_Name = "Texture", .m_Value = std::to_string(m_State->m_TextureId.Value())},
		};
		const bool completed = m_State->m_Phase == State::Phase::Completed;
		diagnostics.m_Checks.push_back({
			.m_Name = "GPU residency",
			.m_Status = !completed ? LabDiagnosticCheckStatus::Pending
				: m_State->m_Passed ? LabDiagnosticCheckStatus::Passed
				: LabDiagnosticCheckStatus::Failed,
			.m_Detail = !completed ? "Verification is running."
				: m_State->m_Passed ? "Release, reload and retirement passed on the device."
				: m_State->m_Errors.front(),
			});
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
		m_State->m_Owner = {};
		GGLAB_LOG_ERROR("ASSET RESIDENCY ACCEPTANCE FAIL: {}", m_State->m_Errors.back());
	}

	void AssetResidencyLabSession::Complete() noexcept
	{
		GGLAB_ASSERT(m_State);
		m_State->m_Passed = true;
		m_State->m_Phase = State::Phase::Completed;
		GGLAB_LOG_INFO(
			"ASSET RESIDENCY ACCEPTANCE PASS: fence-completed release, reload to a generation-safe resident view and retirement passed in {:.2f} s.",
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
			.m_Description =
				"GPU acceptance for fence-completed residency release, reload to resident views and retirement.",
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
