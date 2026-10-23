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

#include <array>
#include <variant>

namespace gglab
{
	namespace
	{
		const LabParameterId AerialPerspectiveId("atmosphere_range.aerial_perspective");
		const LabParameterId TemporalAAId("atmosphere_range.temporal_aa");
		const LabParameterId IBLSamplesId("atmosphere_range.ibl.sample_count");
		constexpr const char* AtmosphereRangeModelPath =
			"Assets/Models/GGLabAtmosphereRange/GGLabAtmosphereRange.gltf";
	}

	std::span<const WorldLightingProfile> AtmosphereRangeLabSession::GetLightingProfiles() const noexcept
	{
		// Fixture inputs, not production Daylight/Golden Hour comparison profiles.
		static const std::array profiles
		{
			WorldLightingProfile{ .m_Id = "atmosphere_range.baseline", .m_Inputs = { .m_SunDirection = Vector3(0.0f, -1.0f, 1.0f) } },
			WorldLightingProfile{ .m_Id = "atmosphere_range.oblique", .m_Inputs = { .m_SunDirection = Vector3(-1.0f, -0.25f, 1.0f) } },
		};
		return profiles;
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
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = IBLSamplesId,
			.m_Name = "IBL Specular Samples",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = IBLBakeConfig{}.m_PrefilteredSpecularSampleCount,
			.m_InheritOnEnter = true,
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(uint32_t(4096)),
			.m_EffectClass = ParameterEffectClass::Deferred,
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

	bool AtmosphereRangeLabSession::RefreshInheritedParameters() noexcept
	{
		if (!m_LightingOverride || m_LightingOverride->OwnsBakeConfiguration())
		{
			return false;
		}
		// An ordinary Runtime writer takes ownership. Follow its value before
		// accepting new edits; unrelated Immediate changes must not reclaim it.
		const uint32_t inherited = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings()
			.m_BakeConfig.m_PrefilteredSpecularSampleCount;
		return GetMutableParameters().FollowInheritedValue(IBLSamplesId, inherited);
	}

	bool AtmosphereRangeLabSession::SupportsImmediateParameter(const LabParameterId& id) const noexcept
	{
		return id == AerialPerspectiveId || id == TemporalAAId;
	}

	std::optional<LabValue> AtmosphereRangeLabSession::ReadImmediateParameterFrameValue(
		const LabParameterId& id, const LabParameterFrameContext& frame) const noexcept
	{
		if (id == AerialPerspectiveId)
		{
			return LabValue(frame.m_ViewSettings.m_Lighting.m_EnableAerialPerspective);
		}
		if (id == TemporalAAId)
		{
			return LabValue(frame.m_ViewSettings.m_TemporalAA.m_Enabled);
		}
		return std::nullopt;
	}

	bool AtmosphereRangeLabSession::SupportsDeferredLightingParameter(const LabParameterId& id) const noexcept
	{
		return id == IBLSamplesId && m_LightingOverride != nullptr;
	}

	LightingMutationResult AtmosphereRangeLabSession::ApplyDeferredLightingParameter(const LabParameterId& id) noexcept
	{
		if (!SupportsDeferredLightingParameter(id))
		{
			return {};
		}
		return m_LightingOverride->ApplyPrefilteredSpecularSampleCount(
			GetParameters().Get(IBLSamplesId, uint32_t(1)));
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
		GGLAB_ASSERT(!m_LightingOverride);
		m_LightingOverride = m_Services.m_EnvironmentLightingControl->AcquireOverride();
		GGLAB_ASSERT_NOT_NULL(m_LightingOverride.get());
		if (!m_LightingOverride)
		{
			return;
		}
		const uint32_t inherited = m_LightingOverride->GetInheritedSettings()
			.m_BakeConfig.m_PrefilteredSpecularSampleCount;
		const bool bound = GetMutableParameters().BindInheritedDefault(IBLSamplesId, inherited);
		GGLAB_ASSERT(bound);
		GGLAB_UNUSED(bound);
		m_LightingOverride->Apply({ .m_Intensity = 1.0f, .m_RotationRadians = 0.0f,
			.m_EnableSkybox = true, .m_BackgroundMode = EnvironmentBackgroundMode::PhysicalSky });
		ApplyParameterChanges(LabChangeImpact::Immediate);
	}

	void AtmosphereRangeLabSession::OnExit() noexcept
	{
		m_LightingOverride.reset();
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
			.m_SchemaVersion = 2,
		};
	}
	std::unique_ptr<LabSessionBase> AtmosphereRangeLabSession::Create(const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AtmosphereRangeLabSession>(createInfo);
	}
}
