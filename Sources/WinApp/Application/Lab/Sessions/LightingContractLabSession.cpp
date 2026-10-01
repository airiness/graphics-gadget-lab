#include "Application/Lab/Sessions/LightingContractLabSession.h"
#include "Application/Lab/LightingContractReferenceViews.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "GGLabRuntime/Scene/Components.h"

#include <cstdint>

namespace gglab
{
	namespace
	{
		const LabParameterId PhysicalSunId("lighting_contract.physical_sun");
		const LabParameterId SceneSourceId("lighting_contract.scene.source");
		constexpr const char* LightingContractModelPath =
			"Assets/Models/GGLabLightingContract/GGLabLightingContract.gltf";
		constexpr const char* MaterialReferenceModelPath =
			"Assets/Models/GGLabLightingContractMaterialReferences/GGLabLightingContract.gltf";

		enum class SceneSource : int32_t
		{
			Original,
			MaterialReferences,
		};
	}

	LightingContractLabSession::LightingContractLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPBR())
	{
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = SceneSourceId,
			.m_Name = "Reference Scene",
			.m_Group = "Scene",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = int32_t(SceneSource::Original),
			.m_EnumItems =
				{
					{.m_Value = int32_t(SceneSource::Original), .m_Name = "Original Contract"},
					{.m_Value = int32_t(SceneSource::MaterialReferences),
						.m_Name = "Extended Material References"},
				},
			}));
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = PhysicalSunId,
			.m_Name = "Physical Sun (120000 lux / EV15 preset)",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		auto& profile = GetMutableViewRenderProfile();
		// Keep reference colors in unit storage; the inspector can override pre-exposure.
		profile.m_EnableScenePreExposure = false;
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;

		auto& cameraRig = GetCameraRig();
		const bool registered = cameraRig.SetReferenceViews(
			{ LightingContractReferenceViews.begin(), LightingContractReferenceViews.end() });
		GGLAB_ASSERT_MSG(registered, "Lighting contract reference view must be valid.");
		const bool restored = cameraRig.RestoreReferenceView(LightingContractReferenceViews.front().m_Id);
		GGLAB_ASSERT_MSG(restored, "Lighting contract must start at its reference view.");
	}

	void LightingContractLabSession::ApplyImmediateParameters() noexcept
	{
		const bool physicalSun = GetParameters().Get(PhysicalSunId, false);
		if (physicalSun == m_PhysicalSun)
		{
			return;
		}
		m_PhysicalSun = physicalSun;
		const bool materialReferences = GetParameters().Get(SceneSourceId,
			int32_t(SceneSource::Original)) == int32_t(SceneSource::MaterialReferences);
		const auto references = BuildLightingContractReferenceViews(physicalSun, materialReferences);
		const bool registered = GetCameraRig().SetReferenceViews({ references.begin(), references.end() });
		GGLAB_ASSERT_MSG(registered, "Lighting contract mode must provide valid reference views.");
		GetMutableViewRenderProfile().m_EnableScenePreExposure = physicalSun;
		GetCamera().SetManualEV100(physicalSun ? 15.0f : 0.0f);
		GetCamera().SetExposureCompensationEV(0.0f);
		for (auto [entity, light] : m_World.GetRegistry().view<components::LightComponent>().each())
		{
			GGLAB_UNUSED(entity);
			if (light.m_Type == LightType::Directional)
			{
				light.m_WorldSun = physicalSun ? std::optional<WorldSunSettings>(WorldSunSettings{}) : std::nullopt;
			}
		}
	}

	void LightingContractLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void LightingContractLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_World.GetRegistry().clear();
		const auto sceneSource = static_cast<SceneSource>(
			GetParameters().Get(SceneSourceId, int32_t(SceneSource::Original)));
		const auto references = BuildLightingContractReferenceViews(m_PhysicalSun,
			sceneSource == SceneSource::MaterialReferences);
		const bool registered = GetCameraRig().SetReferenceViews({ references.begin(), references.end() });
		GGLAB_ASSERT_MSG(registered, "Lighting contract scene reference views must be valid.");
		m_PendingModelPath = sceneSource == SceneSource::MaterialReferences
			? MaterialReferenceModelPath : LightingContractModelPath;
		m_PendingModelId = GetAssetOwnerScope().LoadModelAsync(m_PendingModelPath).m_ModelId;
		m_LoadingProgress = {
			.m_Status = m_PendingModelId.IsValid() ? LoadingStatus::Preparing : LoadingStatus::Failed,
			.m_Fraction = 0.05f,
			.m_Stage = m_PendingModelId.IsValid() ? "Loading lighting contract scene" : "Model request failed",
			.m_Detail = m_PendingModelPath,
		};
	}

	void LightingContractLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}

		const Model* model = m_Services.m_AssetManager->GetModel(m_PendingModelId);
		if (!model)
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Stage = "Model request unavailable";
			return;
		}

		LoadingProgressBuilder progress;
		progress.AddCompletedStep(0.05f);
		progress.AddAssetStep(0.95f,
			GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress),
			m_PendingModelPath);
		m_LoadingProgress = progress.Build();
	}

	void LightingContractLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(), "Lighting contract must be ready before commit.");
		auto& registry = m_World.GetRegistry();
		const entt::entity fixture = registry.create();
		registry.emplace<components::TransformComponent>(fixture);
		registry.emplace<components::ModelComponent>(fixture,
			components::ModelComponent{ .m_ModelId = m_PendingModelId });
		m_PendingModelId.Reset();

		const entt::entity lightEntity = registry.create();
		// Blender ray (0, 1, -1) maps to runtime (0, -1, 1).
		Vector3 direction(0.0f, -1.0f, 1.0f);
		direction.Normalize();
		components::TransformComponent lightTransform{};
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		// Keep the authored intensity; world-sun interpretation is enabled separately.
		light.m_Intensity = 3.0f;
		light.m_Color = Color::White;
		light.m_Type = LightType::Directional;
		light.m_Range = 1000.0f;
		// Leave shadows disabled to isolate material inputs from shadow filtering artifacts.
		if (m_PhysicalSun)
		{
			light.m_WorldSun = WorldSunSettings{};
		}
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void LightingContractLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_PendingModelId.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void LightingContractLabSession::OnEnter() noexcept
	{
		if (m_HasEnvironmentOverride)
		{
			return;
		}

		// Pending sessions must not change the active Lab's environment while loading.
		const auto previous = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
		m_PreviousEnvironmentIntensity = previous.m_Intensity;
		m_PreviousSkyboxEnabled = previous.m_EnableSkybox;
		m_HasEnvironmentOverride = true;
		m_Services.m_EnvironmentLightingControl->SetIntensity(0.0f);
		m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(false);
	}

	void LightingContractLabSession::OnExit() noexcept
	{
		if (m_HasEnvironmentOverride)
		{
			m_Services.m_EnvironmentLightingControl->SetIntensity(m_PreviousEnvironmentIntensity);
			m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(m_PreviousSkyboxEnabled);
			m_HasEnvironmentOverride = false;
		}
		// The base asset scope survives until LabRuntime retires this session after its GPU fence.
	}

	void LightingContractLabSession::Update(float deltaTime) noexcept
	{
		UpdateCamera(deltaTime);
	}

	LabId LightingContractLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.lighting_contract");
	}

	LabDescriptor LightingContractLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Lighting Contract",
			.m_Category = "Lighting",
			.m_Description = "Controlled reflectance, orientation and spheres with legacy or physical directional sun lighting.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> LightingContractLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<LightingContractLabSession>(createInfo);
	}
}
