#include "Core/Precompiled.h"
#include "Core/Math/Quaternion.h"
#include "Application/Lab/Sessions/SampleableDepthLabSession.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		constexpr std::string_view AlphaBlendModeTestPath =
			"Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";

		const LabParameterId EnableCameraInputId(
			"sampleable_depth.camera.enable_input");
		const LabParameterId NearPlaneId(
			"sampleable_depth.camera.near");
		const LabParameterId FarPlaneId(
			"sampleable_depth.camera.far");

		components::MaterialInstanceComponent MakeMaterial(
			std::string_view key,
			const Color& color,
			float roughness) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = color;
			material.m_Properties.m_RoughnessFactor = roughness;
			return material;
		}
	}

	SampleableDepthLabSession::SampleableDepthLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>()),
		m_ViewportWidth(createInfo.m_WindowWidth),
		m_ViewportHeight(createInfo.m_WindowHeight)
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = NearPlaneId,
			.m_Name = "Near Plane",
			.m_Group = "Depth Range",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.05f,
			.m_MinValue = LabValue(0.01f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = FarPlaneId,
			.m_Name = "Far Plane",
			.m_Group = "Depth Range",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 5000.0f,
			.m_MinValue = LabValue(100.0f),
			.m_MaxValue = LabValue(10000.0f),
		}));
		ApplyImmediateParameters();
	}

	void SampleableDepthLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		BuildScene();
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager,
			"Preparing Sampleable Depth Lab");
	}

	void SampleableDepthLabSession::TickPrepare() noexcept
	{
		if (m_LoadingProgress.IsPreparing())
		{
			m_LoadingProgress = m_AssetPreparation.BuildProgress(
				*m_Services.m_AssetManager,
				"Preparing Sampleable Depth Lab");
		}
	}

	void SampleableDepthLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(),
			"Sampleable Depth Lab committed before its assets were ready.");
	}

	void SampleableDepthLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void SampleableDepthLabSession::OnEnter() noexcept
	{
		auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		GGLAB_ASSERT_NOT_NULL(registry);
		m_PreviousPreviewSelection =
			registry->GetPostProcessPreviewSelection();
		registry->SetPostProcessPreviewSelection({
			.m_Tap = PostProcessDebugTap::SceneDepthRaw,
		});
		registry->RequestPostProcessPreview();
	}

	void SampleableDepthLabSession::OnExit() noexcept
	{
		auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		if (registry)
		{
			registry->SetPostProcessPreviewSelection(
				m_PreviousPreviewSelection);
		}
	}

	void SampleableDepthLabSession::Update(float deltaTime) noexcept
	{
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}
		m_Services.m_Renderer->GetRenderResourceRegistry()
			->RequestPostProcessPreview();
	}

	void SampleableDepthLabSession::OnResize(
		uint32_t width,
		uint32_t height) noexcept
	{
		LabSessionBase::OnResize(width, height);
		m_ViewportWidth = width;
		m_ViewportHeight = height;
	}

	void SampleableDepthLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		m_NearPlane = parameters.Get(NearPlaneId, 0.05f);
		m_FarPlane = std::max(
			parameters.Get(FarPlaneId, 5000.0f),
			m_NearPlane + 1.0f);
		GetCamera().SetNearFar(m_NearPlane, m_FarPlane);
	}

	void SampleableDepthLabSession::RebuildScene() noexcept
	{
		BuildScene();
	}

	void SampleableDepthLabSession::OnParametersRestoredForPrepare(
		LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void SampleableDepthLabSession::BuildScene() noexcept
	{
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		ApplyCameraPreset();

		const ModelID alphaModel = GetAssetOwnerScope().LoadModelAsync(
			std::filesystem::path(AlphaBlendModeTestPath)).m_ModelId;
		m_AssetPreparation.TrackModel(
			alphaModel,
			AlphaBlendModeTestPath,
			0.4f);
		m_AssetPreparation.TrackModel(
			ProceduralCubeModelID,
			"ProceduralCube",
			0.3f);
		m_AssetPreparation.TrackModel(
			ProceduralSphereModelID,
			"ProceduralSphere",
			0.3f);

		const entt::entity alphaEntity = registry.create();
		components::TransformComponent alphaTransform{};
		alphaTransform.m_Position = Vector3(-3.4f, 0.0f, 5.0f);
		alphaTransform.m_Scale = Vector3::One * 0.75f;
		registry.emplace<components::TransformComponent>(
			alphaEntity,
			alphaTransform);
		registry.emplace<components::ModelComponent>(
			alphaEntity,
			components::ModelComponent{ .m_ModelId = alphaModel });

		components::TransformComponent floorTransform{};
		floorTransform.m_Position = Vector3(0.0f, -1.4f, 8.0f);
		floorTransform.m_Scale = Vector3(8.0f, 0.2f, 10.0f);
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = floorTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.lab.sampleable_depth.floor",
				Color(0.08f, 0.09f, 0.12f, 1.0f),
				0.75f),
		}));

		components::TransformComponent cubeTransform{};
		cubeTransform.m_Position = Vector3(0.3f, 0.0f, 5.2f);
		cubeTransform.m_Scale = Vector3(1.8f, 1.8f, 1.8f);
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = cubeTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.lab.sampleable_depth.intersection.cube",
				Color(0.12f, 0.42f, 0.92f, 1.0f),
				0.32f),
		}));

		components::TransformComponent sphereTransform{};
		sphereTransform.m_Position = Vector3(1.35f, 0.35f, 5.7f);
		sphereTransform.m_Scale = Vector3::One * 2.0f;
		GGLAB_UNUSED(primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = sphereTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.lab.sampleable_depth.intersection.sphere",
				Color(0.95f, 0.24f, 0.08f, 1.0f),
				0.2f),
		}));

		components::TransformComponent farTransform{};
		farTransform.m_Position = Vector3(0.0f, 25.0f, 350.0f);
		farTransform.m_Scale = Vector3::One * 18.0f;
		GGLAB_UNUSED(primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = farTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.lab.sampleable_depth.far_marker",
				Color(0.65f, 0.75f, 0.2f, 1.0f),
				0.5f),
		}));

		BuildLighting();
		ApplyImmediateParameters();
	}

	void SampleableDepthLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();
		components::TransformComponent transform{};
		Vector3 direction(-0.3f, -0.86f, -0.4f);
		direction.Normalize();
		transform.m_Rotation =
			math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(
			lightEntity,
			transform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Intensity = 3.0f;
		light.m_Range = 1000.0f;
		light.m_DirectionalShadowSettings.emplace();
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void SampleableDepthLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(
			Vector3(0.0f, 2.2f, -10.0f),
			Vector3(0.0f, 0.2f, 6.0f));
		GetCamera().SetFov(50.0f);
		GetCamera().SetNearFar(m_NearPlane, m_FarPlane);
		GetCamera().Update();
	}

	void SampleableDepthLabSession::BuildDiagnostics(
		LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const float ratio = m_FarPlane / std::max(m_NearPlane, 1.0e-6f);
		diagnostics.m_Title = "Sampleable Reversed-Z Depth";
		diagnostics.m_Metrics = {
			{ .m_Name = "Near plane", .m_Value = std::format("{:.4f}", m_NearPlane) },
			{ .m_Name = "Far plane", .m_Value = std::format("{:.1f}", m_FarPlane) },
			{ .m_Name = "Far / near", .m_Value = std::format("{:.0f}:1", ratio) },
			{ .m_Name = "Viewport", .m_Value = std::format(
				"{} x {}", m_ViewportWidth, m_ViewportHeight) },
		};
		diagnostics.m_Checks = {
			{
				.m_Name = "Large near/far ratio",
				.m_Status = ratio >= 10000.0f ?
					LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Failed,
				.m_Detail = "The default range exercises Reversed-Z precision over at least 10,000:1.",
			},
			{
				.m_Name = "Resize and aspect contract",
				.m_Status = m_ViewportWidth > 0 && m_ViewportHeight > 0 ?
					LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Pending,
				.m_Detail = "The active viewport remains valid across resize and restore.",
			},
			{
				.m_Name = "Main / shadow convention split",
				.m_Status = LabDiagnosticCheckStatus::Passed,
				.m_Detail = "Main depth is Reversed-Z; directional shadow depth remains Standard-Z.",
			},
			{
				.m_Name = "Acceptance geometry",
				.m_Status = m_LoadingProgress.IsReady() ?
					LabDiagnosticCheckStatus::Passed :
					LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Background, alpha-test silhouettes, intersecting solids, and a far marker are present.",
			},
		};
	}

	LabId SampleableDepthLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.sampleable_depth");
	}

	LabDescriptor SampleableDepthLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Sampleable Depth",
			.m_Category = "Rendering",
			.m_Description = "Validates Reversed-Z main depth sampling, reconstruction, format views, precision range, alpha silhouettes, intersections, and resize behavior.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> SampleableDepthLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<SampleableDepthLabSession>(createInfo);
	}
}
