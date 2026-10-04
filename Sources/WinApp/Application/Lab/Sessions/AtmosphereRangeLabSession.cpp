#include "Application/Lab/Sessions/AtmosphereRangeLabSession.h"
#include "Application/Lab/AtmosphereRangeReferenceViews.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

namespace gglab
{
	namespace
	{
		const LabParameterId AerialPerspectiveId("atmosphere_range.aerial_perspective");
		const LabParameterId TemporalAAId("atmosphere_range.temporal_aa");
		constexpr const char* AtmosphereRangeModelPath =
			"Assets/Models/GGLabAtmosphereRange/GGLabAtmosphereRange.gltf";
	}

	AtmosphereRangeLabSession::AtmosphereRangeLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPlus())
	{
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = AerialPerspectiveId,
			.m_Name = "Aerial Perspective",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = TemporalAAId,
			.m_Name = "Temporal AA",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		auto& profile = GetMutableViewRenderProfile();
		profile.m_EnableScenePreExposure = true;
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		const bool registered = GetCameraRig().SetReferenceViews(
			{ AtmosphereRangeReferenceViews.begin(), AtmosphereRangeReferenceViews.end() });
		GGLAB_ASSERT_MSG(registered, "Atmosphere range reference views must be valid.");
		const bool restored = GetCameraRig().RestoreReferenceView(AtmosphereRangeReferenceViews.front().m_Id);
		GGLAB_ASSERT_MSG(restored, "Atmosphere range must start at its reference view.");
	}

	void AtmosphereRangeLabSession::ApplyImmediateParameters() noexcept
	{
		auto& profile = GetMutableViewRenderProfile();
		const bool aerial = GetParameters().Get(AerialPerspectiveId, true);
		const bool temporal = GetParameters().Get(TemporalAAId, false);
		if (profile.m_Lighting.m_EnableAerialPerspective != aerial || profile.m_TemporalAA.m_Enabled != temporal)
		{
			// A baseline must not retain history from the previous transport mode.
			GetCamera().RequestTemporalReset();
		}
		profile.m_Lighting.m_EnableAerialPerspective = aerial;
		profile.m_TemporalAA.m_Enabled = temporal;
	}

	void AtmosphereRangeLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void AtmosphereRangeLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_World.GetRegistry().clear();
		m_PendingModelId = GetAssetOwnerScope().LoadModelAsync(AtmosphereRangeModelPath).m_ModelId;
		m_LoadingProgress = {
			.m_Status = m_PendingModelId.IsValid() ? LoadingStatus::Preparing : LoadingStatus::Failed,
			.m_Fraction = 0.05f,
			.m_Stage = m_PendingModelId.IsValid() ? "Loading atmosphere range" : "Model request failed",
			.m_Detail = AtmosphereRangeModelPath,
		};
	}

	void AtmosphereRangeLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing()) return;
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
			AtmosphereRangeModelPath);
		m_LoadingProgress = progress.Build();
	}

	void AtmosphereRangeLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(), "Atmosphere range must be ready before commit.");
		auto& registry = m_World.GetRegistry();
		const entt::entity fixture = registry.create();
		registry.emplace<components::TransformComponent>(fixture);
		registry.emplace<components::ModelComponent>(fixture,
			components::ModelComponent{ .m_ModelId = m_PendingModelId });
		m_PendingModelId.Reset();
		m_World.m_Atmosphere = AtmosphereSettings{};
		const entt::entity sun = registry.create();
		Vector3 direction(0.0f, -1.0f, 1.0f);
		direction.Normalize();
		components::TransformComponent transform{};
		transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(sun, transform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_WorldSun = WorldSunSettings{};
		// Every card has the same direct-light cosine; shadows would obscure the distance test.
		registry.emplace<components::LightComponent>(sun, light);
	}

	void AtmosphereRangeLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_PendingModelId.Reset();
		m_World.GetRegistry().clear();
		m_World.m_Atmosphere.reset();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void AtmosphereRangeLabSession::OnEnter() noexcept
	{
		if (m_HasEnvironmentOverride) return;
		// Loading sessions must not change the active Lab's environment.
		m_PreviousEnvironment = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
		m_HasEnvironmentOverride = true;
		auto& control = *m_Services.m_EnvironmentLightingControl;
		control.SetIntensity(1.0f);
		control.SetRotationRadians(0.0f);
		control.SetSkyboxEnabled(true);
		control.SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
	}

	void AtmosphereRangeLabSession::OnExit() noexcept
	{
		if (!m_HasEnvironmentOverride) return;
		auto& control = *m_Services.m_EnvironmentLightingControl;
		control.SetIntensity(m_PreviousEnvironment.m_Intensity);
		control.SetRotationRadians(m_PreviousEnvironment.m_RotationRadians);
		control.SetSkyboxEnabled(m_PreviousEnvironment.m_EnableSkybox);
		control.SetBackgroundMode(m_PreviousEnvironment.m_BackgroundMode);
		m_HasEnvironmentOverride = false;
		// Asset scope remains owned by this session until LabRuntime's last-use fence retires it.
	}

	void AtmosphereRangeLabSession::Update(float deltaTime) noexcept { UpdateCamera(deltaTime); }
	LabId AtmosphereRangeLabSession::GetId() noexcept { return LabId("gglab.lab.atmosphere_range"); }
	LabDescriptor AtmosphereRangeLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Atmosphere Range",
			.m_Category = "Lighting",
			.m_Description = "Known-distance contrast targets with physical sun, sky and independent aerial-perspective baseline.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}
	std::unique_ptr<LabSessionBase> AtmosphereRangeLabSession::Create(const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AtmosphereRangeLabSession>(createInfo);
	}
}
