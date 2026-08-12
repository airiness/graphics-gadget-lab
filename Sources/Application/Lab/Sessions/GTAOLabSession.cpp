#include "Application/Lab/Sessions/GTAOLabSession.h"
#include "Core/Math/Quaternion.h"

#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Pipeline/GTAO.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		const LabParameterId EnabledId("gtao.enabled");
		const LabParameterId PreviewTapId("gtao.preview_tap");
		const LabParameterId FinalAOFormatId("gtao.final_ao_format");
		const LabParameterId RadiusId("gtao.radius");
		const LabParameterId FalloffStartId("gtao.falloff_start");
		const LabParameterId FalloffEndId("gtao.falloff_end");
		const LabParameterId ThicknessId("gtao.thickness");
		const LabParameterId PowerId("gtao.power");
		const LabParameterId DirectionCountId("gtao.direction_count");
		const LabParameterId StepCountId("gtao.step_count");
		const LabParameterId DenoiseRadiusId("gtao.denoise_radius");
		const LabParameterId EnableCameraInputId("gtao.camera.enable_input");
		const LabParameterId FovId("gtao.camera.fov");
		const LabParameterId NearPlaneId("gtao.camera.near");
		const LabParameterId FarPlaneId("gtao.camera.far");

		components::MaterialInstanceComponent MakeMaterial(
			std::string_view key, const Color& color, float roughness = 0.7f,
			float metallic = 0.0f, const Color& emissive = Color::Black) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = color;
			material.m_Properties.m_RoughnessFactor = roughness;
			material.m_Properties.m_MetallicFactor = metallic;
			material.m_Properties.m_EmissiveColor = emissive;
			return material;
		}
	}

	GTAOLabSession::GTAOLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>()),
		m_ViewportWidth(createInfo.m_WindowWidth), m_ViewportHeight(createInfo.m_WindowHeight)
	{
		auto& profile = GetMutableViewRenderProfile();
		profile.m_Lighting.m_GTAO.m_Enabled = true;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnabledId,
			.m_Name = "Enabled",
			.m_Group = "GTAO",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PreviewTapId,
			.m_Name = "Preview",
			.m_Group = "GTAO",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(PostProcessDebugTap::GTAOFinalAO),
			.m_EnumItems =
				{
					{.m_Value = int32_t(PostProcessDebugTap::GTAORawAO), .m_Name = "Raw AO"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAOHalfDepthViewZ),
						.m_Name = "Half Depth View Z"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAOReconstructedNormal),
						.m_Name = "Reconstructed Normal"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAOSelectedSurfaceOffset),
						.m_Name = "Selected Surface Offset"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAODenoiseX),
						.m_Name = "Denoise X"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAODenoiseY),
						.m_Name = "Denoise Y"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAOFinalAO),
						.m_Name = "Final AO"},
					{.m_Value = int32_t(PostProcessDebugTap::GTAOAOOnlyLightingContribution),
						.m_Name = "AO-only Lighting Contribution"},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FinalAOFormatId,
			.m_Name = "Final AO Format",
			.m_Group = "GTAO",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(GTAOFinalAOFormatPreference::PreferR8Unorm),
			.m_EnumItems =
				{
					{.m_Value = int32_t(GTAOFinalAOFormatPreference::PreferR8Unorm),
						.m_Name = "Prefer R8 Unorm"},
					{.m_Value = int32_t(GTAOFinalAOFormatPreference::ForceR16Float),
						.m_Name = "Force R16 Float"},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RadiusId,
			.m_Name = "Radius",
			.m_Group = "GTAO Spatial",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.01f),
			.m_MaxValue = LabValue(10.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FalloffStartId,
			.m_Name = "Falloff Start",
			.m_Group = "GTAO Spatial",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.1f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(10.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FalloffEndId,
			.m_Name = "Falloff End",
			.m_Group = "GTAO Spatial",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(10.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ThicknessId,
			.m_Name = "Thickness Bias",
			.m_Group = "GTAO Spatial",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.25f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(10.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PowerId,
			.m_Name = "Power",
			.m_Group = "GTAO Spatial",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.1f),
			.m_MaxValue = LabValue(8.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DirectionCountId,
			.m_Name = "Directions",
			.m_Group = "GTAO Quality",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(2),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(GTAOMaxDirectionCount),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = StepCountId,
			.m_Name = "Steps",
			.m_Group = "GTAO Quality",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(4),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(GTAOMaxStepCount),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DenoiseRadiusId,
			.m_Name = "Denoise Radius",
			.m_Group = "GTAO Quality",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(3),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(GTAOMaxDenoiseRadius),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FovId,
			.m_Name = "Vertical FOV",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 50.0f,
			.m_MinValue = LabValue(25.0f),
			.m_MaxValue = LabValue(90.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = NearPlaneId,
			.m_Name = "Near Plane",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.05f,
			.m_MinValue = LabValue(0.01f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FarPlaneId,
			.m_Name = "Far Plane",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1000.0f,
			.m_MinValue = LabValue(100.0f),
			.m_MaxValue = LabValue(10000.0f),
			}));
		ApplyImmediateParameters();
	}

	void GTAOLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		BuildScene();
		m_LoadingProgress =
			m_AssetPreparation.BuildProgress(*m_Services.m_AssetManager, "Preparing GTAO Lab");
	}

	void GTAOLabSession::TickPrepare() noexcept
	{
		if (m_LoadingProgress.IsPreparing())
		{
			m_LoadingProgress = m_AssetPreparation.BuildProgress(
				*m_Services.m_AssetManager, "Preparing GTAO Lab");
		}
	}

	void GTAOLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(), "GTAO Lab committed before its assets were ready.");
	}

	void GTAOLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void GTAOLabSession::OnEnter() noexcept
	{
		auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		m_PreviousPreviewSelection = registry->GetPostProcessPreviewSelection();
		m_PreviewUpdateCountOnEnter = registry->GetPostProcessPreviewUpdateCount();
		RequestSelectedPreview();
	}

	void GTAOLabSession::OnExit() noexcept
	{
		if (auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry())
		{
			registry->SetPostProcessPreviewSelection(m_PreviousPreviewSelection);
			registry->RequestPostProcessPreview();
		}
	}

	void GTAOLabSession::Update(float deltaTime) noexcept
	{
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}
		RequestSelectedPreview();
	}

	void GTAOLabSession::OnResize(uint32_t width, uint32_t height) noexcept
	{
		LabSessionBase::OnResize(width, height);
		m_ViewportWidth = width;
		m_ViewportHeight = height;
	}

	void GTAOLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		GetMutableViewRenderProfile().m_Lighting.m_GTAO.m_Enabled =
			parameters.Get(EnabledId, true);
		m_SelectedTap = static_cast<PostProcessDebugTap>(parameters.Get(
			PreviewTapId, int32_t(PostProcessDebugTap::GTAOFinalAO)));
		GetMutableViewRenderProfile().m_Lighting.m_GTAO.m_FinalAOFormatPreference =
			static_cast<GTAOFinalAOFormatPreference>(parameters.Get(FinalAOFormatId,
				int32_t(GTAOFinalAOFormatPreference::PreferR8Unorm)));
		auto& gtao = GetMutableViewRenderProfile().m_Lighting.m_GTAO;
		gtao.m_Radius = parameters.Get(RadiusId, 1.0f);
		gtao.m_FalloffStart = parameters.Get(FalloffStartId, 0.1f);
		gtao.m_FalloffEnd = parameters.Get(FalloffEndId, 1.0f);
		gtao.m_Thickness = parameters.Get(ThicknessId, 0.25f);
		gtao.m_Power = parameters.Get(PowerId, 1.0f);
		gtao.m_DirectionCount = parameters.Get(DirectionCountId, uint32_t(2));
		gtao.m_StepCount = parameters.Get(StepCountId, uint32_t(4));
		gtao.m_DenoiseRadius = parameters.Get(DenoiseRadiusId, uint32_t(3));
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, false);
		m_FovDegrees = parameters.Get(FovId, 50.0f);
		m_NearPlane = parameters.Get(NearPlaneId, 0.05f);
		m_FarPlane = std::max(parameters.Get(FarPlaneId, 1000.0f), m_NearPlane + 1.0f);
		GetCamera().SetFov(m_FovDegrees);
		GetCamera().SetNearFar(m_NearPlane, m_FarPlane);
		RequestSelectedPreview();
	}

	void GTAOLabSession::RebuildScene() noexcept
	{
		BuildScene();
	}

	void GTAOLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void GTAOLabSession::BuildScene() noexcept
	{
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		m_FixtureConfigured = false;
		ApplyCameraPreset();
		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.6f);
		m_AssetPreparation.TrackModel(ProceduralSphereModelID, "ProceduralSphere", 0.4f);

		const auto createCube = [this](std::string_view key, const Vector3& position,
			const Vector3& scale, const Color& color) noexcept
			{
				components::TransformComponent transform{};
				transform.m_Position = position;
				transform.m_Scale = scale;
				return primitive::Cube::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
					.m_World = &m_World,
					.m_Transform = transform,
					.m_MaterialInstance = MakeMaterial(key, color),
					});
			};

		const entt::entity floor = createCube("gglab.lab.gtao.floor",
			Vector3(0.0f, -1.5f, 7.0f), Vector3(8.0f, 0.2f, 9.0f),
			Color(0.14f, 0.16f, 0.19f, 1.0f));
		const entt::entity cornerWall = createCube("gglab.lab.gtao.corner_wall",
			Vector3(-5.0f, 1.4f, 8.0f), Vector3(0.2f, 3.0f, 5.0f),
			Color(0.25f, 0.28f, 0.32f, 1.0f));
		const entt::entity thinSlab = createCube("gglab.lab.gtao.thin_slab",
			Vector3(2.8f, 0.1f, 5.2f), Vector3(0.08f, 1.7f, 1.6f),
			Color(0.75f, 0.34f, 0.12f, 1.0f));
		const entt::entity edgeSlab = createCube("gglab.lab.gtao.screen_edge_slab",
			Vector3(6.0f, 0.2f, 6.5f), Vector3(0.3f, 1.8f, 1.2f),
			Color(0.68f, 0.58f, 0.12f, 1.0f));
		const entt::entity tieLeft = createCube("gglab.lab.gtao.tie_left",
			Vector3(-0.65f, 0.1f, 5.5f), Vector3(0.65f, 1.2f, 0.08f),
			Color(0.16f, 0.42f, 0.78f, 1.0f));
		const entt::entity tieRight = createCube("gglab.lab.gtao.tie_right",
			Vector3(0.65f, 0.1f, 5.5f), Vector3(0.65f, 1.2f, 0.08f),
			Color(0.22f, 0.68f, 0.46f, 1.0f));
		const entt::entity emissiveControl = primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = components::TransformComponent{
				.m_Position = Vector3(-3.0f, -0.3f, 4.8f),
				.m_Scale = Vector3(0.55f, 0.55f, 0.55f),
				},
			.m_MaterialInstance = MakeMaterial("gglab.lab.gtao.emissive_control",
				Color(0.03f, 0.03f, 0.03f, 1.0f), 0.7f, 0.0f,
				Color(2.0f, 0.4f, 0.08f, 1.0f)),
			});
		const entt::entity specularControl = primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = components::TransformComponent{
				.m_Position = Vector3(4.0f, -0.25f, 8.2f),
				.m_Scale = Vector3(0.65f, 0.65f, 0.65f),
				},
			.m_MaterialInstance = MakeMaterial("gglab.lab.gtao.specular_control",
				Color(0.85f, 0.88f, 0.92f, 1.0f), 0.05f, 1.0f),
			});
		const entt::entity radiusNearGap = createCube("gglab.lab.gtao.radius_near_gap",
			Vector3(-2.0f, -0.75f, 8.8f), Vector3(0.35f, 0.35f, 0.35f),
			Color(0.62f, 0.36f, 0.82f, 1.0f));
		const entt::entity radiusFarGap = createCube("gglab.lab.gtao.radius_far_gap",
			Vector3(-1.0f, -0.35f, 8.8f), Vector3(0.35f, 0.35f, 0.35f),
			Color(0.42f, 0.72f, 0.86f, 1.0f));
		const entt::entity haloBackground = createCube("gglab.lab.gtao.halo_background",
			Vector3(4.8f, 0.3f, 10.5f), Vector3(1.1f, 1.5f, 0.08f),
			Color(0.7f, 0.72f, 0.76f, 1.0f));
		const entt::entity haloOccluder = createCube("gglab.lab.gtao.halo_occluder",
			Vector3(4.8f, 0.3f, 9.8f), Vector3(0.22f, 0.9f, 0.08f),
			Color(0.18f, 0.2f, 0.24f, 1.0f));

		components::TransformComponent sphereTransform{};
		sphereTransform.m_Position = Vector3(1.4f, -0.1f, 8.0f);
		sphereTransform.m_Scale = Vector3::One * 1.5f;
		const entt::entity sphere = primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = sphereTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.lab.gtao.silhouette", Color(0.72f, 0.18f, 0.22f, 1.0f), 0.35f),
			});

		const entt::entity fixtures[] = { floor, cornerWall, thinSlab, edgeSlab, tieLeft,
			tieRight, emissiveControl, specularControl, radiusNearGap, radiusFarGap,
			haloBackground, haloOccluder, sphere };
		m_FixtureConfigured = std::ranges::all_of(fixtures, [&registry](entt::entity entity)
			{
				return registry.valid(entity) &&
					registry.all_of<components::TransformComponent, components::ModelComponent,
					components::MaterialInstanceComponent>(entity);
			});
		BuildLighting();
	}

	void GTAOLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();
		components::TransformComponent transform{};
		Vector3 direction(-0.35f, -0.85f, -0.4f);
		direction.Normalize();
		transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, transform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Intensity = 3.0f;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void GTAOLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(0.0f, 2.3f, -9.0f), Vector3(0.0f, 0.0f, 6.5f));
		GetCamera().SetFov(m_FovDegrees);
		GetCamera().SetNearFar(m_NearPlane, m_FarPlane);
		GetCamera().Update();
	}

	void GTAOLabSession::RequestSelectedPreview() noexcept
	{
		if (!m_Services.m_Renderer)
		{
			return;
		}
		auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		if (!registry)
		{
			return;
		}
		registry->SetPostProcessPreviewSelection({ .m_Tap = m_SelectedTap });
		registry->RequestPostProcessPreview();
	}

	void GTAOLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const GTAOExtent halfExtent =
			MakeGTAOHalfResolutionExtent(m_ViewportWidth, m_ViewportHeight);
		const GTAOSettings& settings = GetViewRenderProfile().m_Lighting.m_GTAO;
		const auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		const bool previewExecuted = registry && registry->HasPublishedPostProcessPreview() &&
			registry->GetPostProcessPreviewUpdateCount() > m_PreviewUpdateCountOnEnter &&
			registry->GetPublishedPostProcessPreviewSelection().m_Tap == m_SelectedTap;
		diagnostics.m_Title = "GTAO Spatial Pipeline";
		diagnostics.m_Metrics = {
			{.m_Name = "Full extent",
				.m_Value = std::format("{} x {}", m_ViewportWidth, m_ViewportHeight)},
			{.m_Name = "Half extent",
				.m_Value = std::format("{} x {}", halfExtent.m_Width, halfExtent.m_Height)},
			{.m_Name = "Evaluate kernel",
				.m_Value = std::format("{} directions x {} steps", settings.m_DirectionCount,
					settings.m_StepCount)},
			{.m_Name = "Spatial settings",
				.m_Value = std::format("radius {:.2f} m, thickness {:.2f} m, power {:.2f}",
					settings.m_Radius, settings.m_Thickness, settings.m_Power)},
			{.m_Name = "Denoise kernel",
				.m_Value = std::format("separable bilateral, radius {}", settings.m_DenoiseRadius)},
			{.m_Name = "Performance target", .m_Value = "~1.5 ms at default 1440p (informational)"},
			{.m_Name = "PBR integration",
				.m_Value = GetViewRenderProfile().m_Lighting.m_GTAO.m_Enabled
					? "indirect diffuse"
					: "neutral disabled path"},
		};
		diagnostics.m_Checks = {
			{
				.m_Name = "Deterministic fixture",
				.m_Status = m_FixtureConfigured ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail =
					"Plane, corner, thin and screen-edge slabs, silhouette, radius-gap and halo "
					"fixtures, emissive/specular controls, background, and coplanar ties are configured.",
			},
			{
				.m_Name = "Half-resolution contract",
				.m_Status = halfExtent.IsValid() ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Failed,
				.m_Detail = "The evaluate extent uses ceil(full extent / 2) on each axis.",
			},
			{
				.m_Name = "Selected preview published",
				.m_Status = previewExecuted ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "The selected GTAO pipeline surface was published after Lab entry.",
			},
		};
	}

	LabId GTAOLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.gtao");
	}

	LabDescriptor GTAOLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "GTAO",
			.m_Category = "Rendering",
			.m_Description =
				"Validates deterministic GTAO surface selection, depth-derived normals, "
				"bilateral denoise, and depth-aware upsample.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> GTAOLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<GTAOLabSession>(createInfo);
	}
}
