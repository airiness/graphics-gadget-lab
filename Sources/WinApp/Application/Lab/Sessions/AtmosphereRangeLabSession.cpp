#include "Application/Lab/Sessions/AtmosphereRangeLabSession.h"
#include "Application/Lab/AtmosphereRangeReferenceViews.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

#include <array>
#include <cmath>
#include <variant>

namespace gglab
{
	namespace
	{
		const LabParameterId AerialPerspectiveId("atmosphere_range.aerial_perspective");
		const LabParameterId TemporalAAId("atmosphere_range.temporal_aa");
		const LabParameterId IBLSamplesId("atmosphere_range.ibl.sample_count");
		const LabParameterId SunElevationId("atmosphere_range.sun.elevation_degrees");
		constexpr float DefaultSunElevation = 45.0f;
		constexpr const char* AtmosphereRangeModelPath =
			"Assets/Models/GGLabAtmosphereRange/GGLabAtmosphereRange.gltf";

		float ReadSunElevation(const Vector3& direction) noexcept
		{
			return math::ToDegrees(std::atan2(-direction.m_Y,
				std::sqrt(direction.m_X * direction.m_X + direction.m_Z * direction.m_Z)));
		}

		Vector3 WithSunElevation(const Vector3& direction, float degrees) noexcept
		{
			Vector3 horizontal(direction.m_X, 0.0f, direction.m_Z);
			if (horizontal.LengthSquared() > 1.0e-8f)
			{
				horizontal.Normalize();
			}
			else
			{
				horizontal = Vector3::Forward;
			}
			const float angle = math::ToRadians(degrees);
			return horizontal * std::cos(angle) - Vector3::UnitY * std::sin(angle);
		}
	}

	std::span<const WorldLightingProfile> AtmosphereRangeLabSession::GetLightingProfiles() const noexcept
	{
		// Fixture inputs, not production Daylight/Golden Hour comparison profiles.
		static const std::array profiles
		{
			WorldLightingProfile{ .m_Id = "atmosphere_range.baseline",
				.m_Inputs = { .m_SunDirection = WithSunElevation(Vector3::Forward, DefaultSunElevation) } },
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
		GGLAB_UNUSED(GetMutableParameters().Add({
			.m_Id = SunElevationId,
			.m_Name = "Sun Elevation (degrees)",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_EditPolicy = LabParameterEditPolicy::CommitOnEditEnd,
			.m_DefaultValue = DefaultSunElevation,
			.m_InheritOnEnter = true,
			.m_MinValue = LabValue(-90.0f),
			.m_MaxValue = LabValue(90.0f),
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
		const auto& overview = AtmosphereRangeReferenceViews.front();
		const bool pathRegistered = GetCameraRig().SetCameraPaths({ {
			.m_Id = "SEQ_AtmosphereRange_Static",
			.m_Name = "Atmosphere Range Static",
			.m_Purpose = "Hold the overview through asynchronous lighting transitions.",
			.m_Version = 1,
			.m_NearPlane = GetCamera().GetNear(),
			.m_FarPlane = GetCamera().GetFar(),
			.m_ManualEV100 = GetCamera().GetManualEV100(),
			.m_ExposureCompensationEV = GetCamera().GetExposureCompensationEV(),
			.m_Keys = {
				{ .m_Frame = 0, .m_Position = overview.m_Position, .m_Target = overview.m_Target,
					.m_VerticalFovDegrees = overview.m_VerticalFovDegrees },
				{ .m_Frame = 239, .m_Position = overview.m_Position, .m_Target = overview.m_Target,
					.m_VerticalFovDegrees = overview.m_VerticalFovDegrees },
			},
		} });
		GGLAB_ASSERT_MSG(pathRegistered, "Atmosphere range must register its lighting timeline.");
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
		bool changed = false;
		if (m_World.GetRegistry().valid(m_SunEntity))
		{
			const auto& rotation = m_World.GetRegistry().get<components::TransformComponent>(m_SunEntity).m_Rotation;
			const auto direction = ResolveWorldSunDirection(rotation);
			const float degrees = GetParameters().Get(SunElevationId, DefaultSunElevation);
			const auto expected = ResolveWorldSunDirection(ResolveWorldLightingRotation(
				WithSunElevation(m_SunHorizontalDirection, degrees)));
			if (direction.m_X != expected.m_X || direction.m_Y != expected.m_Y || direction.m_Z != expected.m_Z)
			{
				Vector3 horizontal(direction.m_X, 0.0f, direction.m_Z);
				if (horizontal.LengthSquared() > 1.0e-8f)
				{
					horizontal.Normalize();
					m_SunHorizontalDirection = horizontal;
				}
			}
			const float current = ReadSunElevation(direction);
			// Keep exact requested degrees through rotation round trips; a foreign
			// authored direction releases this Lab's intent and becomes the current value.
			if (std::abs(current - degrees) > 1.0e-4f)
			{
				changed |= GetMutableParameters().FollowInheritedValue(SunElevationId, current);
			}
		}
		// An ordinary Runtime writer takes ownership. Follow its value before
		// accepting new edits; unrelated Immediate changes must not reclaim it.
		if (m_LightingOverride && !m_LightingOverride->OwnsBakeConfiguration())
		{
			const uint32_t inherited = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings()
				.m_BakeConfig.m_PrefilteredSpecularSampleCount;
			changed |= GetMutableParameters().FollowInheritedValue(IBLSamplesId, inherited);
		}
		return changed;
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
		return (id == IBLSamplesId && m_LightingOverride != nullptr) || SupportsWorldLightingParameter(id);
	}

	LightingMutationResult AtmosphereRangeLabSession::ApplyDeferredLightingParameter(const LabParameterId& id) noexcept
	{
		if (!SupportsDeferredLightingParameter(id))
		{
			return {};
		}
		if (id == SunElevationId)
		{
			// Interactive/startup Lab edits use the same safe command boundary. Session
			// batches instead commit the prepared World Lighting operation below.
			auto& rotation = m_World.GetRegistry().get<components::TransformComponent>(m_SunEntity).m_Rotation;
			rotation = ResolveWorldLightingRotation(WithSunElevation(m_SunHorizontalDirection,
				GetParameters().Get(SunElevationId, DefaultSunElevation)));
			return {};
		}
		return m_LightingOverride->ApplyPrefilteredSpecularSampleCount(
			GetParameters().Get(IBLSamplesId, uint32_t(1)));
	}

	bool AtmosphereRangeLabSession::SupportsWorldLightingParameter(const LabParameterId& id) const noexcept
	{
		return id == SunElevationId && m_World.GetRegistry().valid(m_SunEntity);
	}

	std::unique_ptr<PreparedWorldLightingMutationBase> AtmosphereRangeLabSession::PrepareWorldLightingParameter(
		const LabParameterId& id, const LabValue& value, WorldLightingControlBase& control,
		uint64_t contentIdentity, std::string& error) noexcept
	{
		const auto publication = control.GetWorldLightingPublication();
		const auto* degrees = std::get_if<float>(&value);
		if (!SupportsWorldLightingParameter(id) || !degrees || !publication.m_Requested ||
			publication.m_Requested->m_SessionIdentity != contentIdentity)
		{
			error = "LightingPreparationFailed: the Lab's authored World Lighting state is unavailable.";
			return {};
		}
		auto inputs = publication.m_Requested->m_Inputs;
		// Retain the Lab's authored horizontal direction through its own rotation
		// round trips. Re-deriving it from each resolved request drifts by ULPs and
		// would turn an identical elevation request into another Runtime generation.
		inputs.m_SunDirection = WithSunElevation(m_SunHorizontalDirection, *degrees);
		return control.PrepareWorldLightingInputs(m_World, contentIdentity, inputs, error);
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
		m_SunHorizontalDirection = Vector3::Forward;
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
		m_SunEntity = registry.create();
		const Vector3 direction = WithSunElevation(Vector3::Forward, DefaultSunElevation);
		components::TransformComponent transform{};
		transform.m_Rotation = ResolveWorldLightingRotation(direction);
		registry.emplace<components::TransformComponent>(m_SunEntity, transform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_WorldSun = WorldSunSettings{};
		// Every card has the same direct-light cosine; shadows would obscure the distance test.
		registry.emplace<components::LightComponent>(m_SunEntity, light);
	}

	void AtmosphereRangeLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_PendingModelId.Reset();
		m_World.GetRegistry().clear();
		m_SunEntity = entt::null;
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
		const bool sunBound = GetMutableParameters().BindInheritedDefault(SunElevationId, DefaultSunElevation);
		GGLAB_ASSERT(sunBound);
		GGLAB_UNUSED(sunBound);
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
			.m_SchemaVersion = 3,
		};
	}
	std::unique_ptr<LabSessionBase> AtmosphereRangeLabSession::Create(const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AtmosphereRangeLabSession>(createInfo);
	}
}
