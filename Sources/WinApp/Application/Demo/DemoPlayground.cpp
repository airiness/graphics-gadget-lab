#include "Application/Demo/DemoPlayground.h"
#include "ApplicationCameraInput.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Time.h"
#include "Scene/Components.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Asset/Loading/AssetLoadProgress.h"
#include "Graphics/Asset/AssetManager.h"

namespace gglab
{
	DemoPlayground::DemoPlayground(const DemoCreateInfo& createInfo) noexcept :
		m_Services(createInfo.m_Services),
		m_AssetOwnerScope(createInfo.m_Services.m_AssetManager->CreateOwnerScope())
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "DemoPlayground requires valid create info.");

		// Camera
		Camera::CreateInfo camCreateInfo{};
		camCreateInfo.m_Forward = -Vector3::UnitX;
		camCreateInfo.m_Position = Vector3(8.0f, 2.0f, 0.0f);
		camCreateInfo.m_Width = createInfo.m_WindowWidth;
		camCreateInfo.m_Height = createInfo.m_WindowHeight;
		camCreateInfo.m_Near = 0.1f;
		camCreateInfo.m_Far = 1000.0f;
		camCreateInfo.m_Fov = 60.0f;
		m_Camera = std::make_unique<Camera>(camCreateInfo);

		// CameraController
		CameraController::CreateInfo camCtrlCreateInfo{};
		camCtrlCreateInfo.m_Params.m_MovementSpeed = 10.0f;
		camCtrlCreateInfo.m_Params.m_MouseSensitivityRadPerCount = 0.0018f;
		camCtrlCreateInfo.m_Params.m_AccelerateMultiplier = 3.0f;
		camCtrlCreateInfo.m_Params.m_SmoothStepT = 0.5f;
		m_CameraController = std::make_unique<CameraController>(camCtrlCreateInfo);
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);

		// RenderPipeline
		m_RenderPipeline = std::make_unique<RenderPipelineForwardPBR>();
	}

	void DemoPlayground::BeginPrepare() noexcept
	{
		m_AssetOwnerScope.Reset();
		m_World.GetRegistry().clear();
		m_PendingModels = {
			{
				.m_Path = "Assets/Models/Sponza/Sponza.gltf",
				.m_Position = Vector3::Zero,
				.m_Rotation = Vector3::Zero,
				.m_Scale = Vector3::One,
			},
			{
				.m_Path = "Assets/Models/FlightHelmet/FlightHelmet.gltf",
				.m_Position = Vector3::Zero,
				.m_Rotation = Vector3::Zero,
				.m_Scale = Vector3::One,
			},
		};

		auto* assetManager = m_Services.m_AssetManager;
		GGLAB_ASSERT_NOT_NULL(assetManager);
		for (PendingModel& pending : m_PendingModels)
		{
			pending.m_ModelId = m_AssetOwnerScope.LoadModelAsync(pending.m_Path).m_ModelId;
			if (!pending.m_ModelId.IsValid())
			{
				m_LoadingProgress = {
					.m_Status = LoadingStatus::Failed,
					.m_Fraction = 0.0f,
					.m_Stage = "Model request failed",
					.m_Detail = pending.m_Path.generic_string(),
				};
				return;
			}
		}

		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.05f,
			.m_Stage = "Loading scene models",
			.m_Detail = m_PendingModels.front().m_Path.generic_string(),
		};
	}

	void DemoPlayground::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}

		LoadingProgressBuilder progress;
		progress.AddCompletedStep(0.05f);
		const float modelWeight =
			m_PendingModels.empty() ? 0.0f : 0.95f / static_cast<float>(m_PendingModels.size());
		for (const PendingModel& pending : m_PendingModels)
		{
			const Model* model = m_Services.m_AssetManager->GetModel(pending.m_ModelId);
			if (!model)
			{
				progress.AddStep(modelWeight,
					{
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.0f,
						.m_Stage = "Model request unavailable",
						.m_Detail = pending.m_Path.generic_string(),
					});
				continue;
			}

			progress.AddAssetStep(modelWeight,
				GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress),
				pending.m_Path.generic_string());
		}

		m_LoadingProgress = progress.Build();
		if (!m_LoadingProgress.IsReady())
		{
			return;
		}

		m_LoadingProgress = {
			.m_Status = LoadingStatus::Ready,
			.m_Fraction = 1.0f,
			.m_Stage = "Scene ready",
			.m_Detail = "All models and GPU resources are ready.",
		};
	}

	void DemoPlayground::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(), "DemoPlayground committed before preparation completed.");
		CommitScene();
		m_PendingModels.clear();
	}

	void DemoPlayground::CancelPrepare() noexcept
	{
		m_AssetOwnerScope.Reset();
		m_PendingModels.clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void DemoPlayground::OnEnter() noexcept
	{
	}

	void DemoPlayground::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_CameraRig.OnResize(width, height);
	}

	void DemoPlayground::OnExit() noexcept
	{
		m_AssetOwnerScope.Reset();
	}

	void DemoPlayground::Update() noexcept
	{
		auto* input = m_Services.m_Input;
		auto* time = m_Services.m_Time;
		GGLAB_ASSERT_NOT_NULL(input);
		GGLAB_ASSERT_NOT_NULL(time);
		const auto deltaTime = static_cast<float>(time->GetDeltaTime());

		Camera& camera = m_CameraRig.GetActiveCamera();
		CameraController& controller = m_CameraRig.GetActiveCameraController();
		controller.Update(camera, BuildCameraInput(*input), deltaTime);
		camera.Update();
	}

	void DemoPlayground::CommitScene() noexcept
	{
		auto& registry = m_World.GetRegistry();
		for (const PendingModel& pending : m_PendingModels)
		{
			auto entity = registry.create();
			components::TransformComponent transformComp{};
			transformComp.m_Position = pending.m_Position;
			transformComp.m_Rotation = math::CreateFromYawPitchRoll(
				math::ToRadians(pending.m_Rotation.m_Y), math::ToRadians(pending.m_Rotation.m_X),
				math::ToRadians(pending.m_Rotation.m_Z));
			transformComp.m_Scale = pending.m_Scale;
			registry.emplace<components::TransformComponent>(entity, transformComp);

			components::ModelComponent modelComp{};
			modelComp.m_ModelId = pending.m_ModelId;
			registry.emplace<components::ModelComponent>(entity, modelComp);
		}

		// Main Light
		{
			auto mainLightEntity = registry.create();

			components::TransformComponent transComp{};
			Vector3 direction = Vector3(-0.406f, -0.906f, -0.123f);
			direction.Normalize();
			transComp.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			registry.emplace<components::TransformComponent>(mainLightEntity, transComp);

			components::LightComponent lightComp{};
			lightComp.m_Intensity = 3.0f;
			lightComp.m_Color = Color::White;
			lightComp.m_Type = LightType::Directional;
			lightComp.m_Range = 1000.0f;
			lightComp.m_DirectionalShadowSettings.emplace();
			registry.emplace<components::LightComponent>(mainLightEntity, lightComp);
		}
	}
}
