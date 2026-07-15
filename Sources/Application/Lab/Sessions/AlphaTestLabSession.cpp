#include "Core/Precompiled.h"
#include "Core/Math/Quaternion.h"
#include "Application/Lab/Sessions/AlphaTestLabSession.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		constexpr std::string_view AlphaBlendModeTestPath =
			"Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";

		const LabParameterId EnableCameraInputId("alpha_test.camera.enable_input");
		const LabParameterId CameraFovId("alpha_test.camera.fov");
		const LabParameterId ModelScaleId("alpha_test.model.scale");
		const LabParameterId LightIntensityId("alpha_test.lighting.intensity");
	}

	AlphaTestLabSession::AlphaTestLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(
			GetDescriptor(),
			createInfo,
			std::make_unique<RenderPipelineForwardPBR>())
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
			.m_Id = CameraFovId,
			.m_Name = "Field of View",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 45.0f,
			.m_MinValue = LabValue(30.0f),
			.m_MaxValue = LabValue(90.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ModelScaleId,
			.m_Name = "Model Scale",
			.m_Group = "Model",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.25f),
			.m_MaxValue = LabValue(4.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = LightIntensityId,
			.m_Name = "Intensity",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 3.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(20.0f),
		}));

		ApplyImmediateParameters();
	}

	void AlphaTestLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		RebuildScene();
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager,
			"Preparing Alpha Test");
	}

	void AlphaTestLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager,
			"Preparing Alpha Test");
	}

	void AlphaTestLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(),
			"Alpha Test committed before its model was ready.");
	}

	void AlphaTestLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void AlphaTestLabSession::Update(float deltaTime) noexcept
	{
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else
		{
			GetCamera().Update();
		}
	}

	void AlphaTestLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		GetCamera().SetFov(parameters.Get(CameraFovId, 45.0f));

		auto lightView = m_World.GetRegistry().view<components::LightComponent>();
		for (const entt::entity entity : lightView)
		{
			lightView.get<components::LightComponent>(entity).m_Intensity =
				parameters.Get(LightIntensityId, 3.0f);
		}
	}

	void AlphaTestLabSession::RebuildScene() noexcept
	{
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();

		ApplyCameraPreset();

		auto* assetManager = m_Services.m_AssetManager;
		GGLAB_ASSERT_NOT_NULL(assetManager);
		const ModelID modelId = GetAssetOwnerScope().LoadModelAsync(
			std::filesystem::path(AlphaBlendModeTestPath)).m_ModelId;
		m_AssetPreparation.TrackModel(modelId, AlphaBlendModeTestPath);

		const entt::entity modelEntity = registry.create();
		components::TransformComponent modelTransform{};
		modelTransform.m_Position = Vector3::Zero;
		modelTransform.m_Scale = Vector3::One * GetParameters().Get(ModelScaleId, 1.0f);
		registry.emplace<components::TransformComponent>(modelEntity, modelTransform);
		registry.emplace<components::ModelComponent>(modelEntity, components::ModelComponent{
			.m_ModelId = modelId,
		});

		BuildLighting();
		ApplyImmediateParameters();
	}

	void AlphaTestLabSession::OnParametersRestoredForPrepare(
		LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void AlphaTestLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();

		components::TransformComponent lightTransform{};
		Vector3 direction(-0.35f, -0.82f, -0.45f);
		direction.Normalize();
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Range = 1000.0f;
		light.m_DirectionalShadowSettings.emplace();
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void AlphaTestLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(
			Vector3(0.0f, 1.0f, -9.0f),
			Vector3(0.0f, 0.8f, 0.0f));
		GetCamera().Update();
	}

	LabId AlphaTestLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.alpha_test");
	}

	LabDescriptor AlphaTestLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Alpha Test",
			.m_Category = "Materials",
			.m_Description = "Validates alpha mask, alpha blend and opaque material rendering using the AlphaBlendModeTest glTF asset.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> AlphaTestLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<AlphaTestLabSession>(createInfo);
	}
}
