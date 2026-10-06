#include "Application/Demo/DemoPlayground.h"
#include "Application/Demo/CoastalSceneReferenceViews.h"
#include "ApplicationCameraInput.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/WorldSun.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"

#include <optional>

namespace gglab
{
	DemoPlayground::DemoPlayground(const DemoCreateInfo& createInfo,
		PlaygroundContent content) noexcept :
		m_Services(createInfo.m_Services),
		m_Content(content),
		m_AssetOwnerScope(createInfo.m_Services.m_AssetManager->CreateOwnerScope())
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "DemoPlayground requires valid create info.");
		GGLAB_ASSERT_MSG(m_Content == PlaygroundContent::Island ||
			m_Content == PlaygroundContent::CoastalAtrium, "DemoPlayground requires a supported content preset.");

		// Camera
		Camera::CreateInfo camCreateInfo{};
		camCreateInfo.m_Width = createInfo.m_WindowWidth;
		camCreateInfo.m_Height = createInfo.m_WindowHeight;
		camCreateInfo.m_Near = 0.1f;
		// Blender (X, Y, Z) maps to runtime (X, Z, Y); preserve authored meters.
		camCreateInfo.m_Position = Vector3(17.0f, 16.0f, -23.0f);
		camCreateInfo.m_Forward = Vector3(0.0f, 0.8f, 0.0f) - camCreateInfo.m_Position;
		camCreateInfo.m_Forward.Normalize();
		camCreateInfo.m_Far = 100.0f;
		camCreateInfo.m_Fov = math::ToDegrees(0.4426289085f);
		camCreateInfo.m_ExposureCompensationEV = 0.0f;
		m_ViewRenderProfile.m_TemporalAA.m_Enabled = m_Content == PlaygroundContent::CoastalAtrium;
		m_ViewRenderProfile.m_Lighting.m_GTAO.m_Enabled = false;
		m_ViewRenderProfile.m_PostProcess.m_Bloom.m_Enabled = false;
		m_Camera = std::make_unique<Camera>(camCreateInfo);

		// CameraController
		CameraController::CreateInfo camCtrlCreateInfo{};
		camCtrlCreateInfo.m_Params.m_MovementSpeed = 10.0f;
		camCtrlCreateInfo.m_Params.m_MouseSensitivityRadPerCount = 0.0018f;
		camCtrlCreateInfo.m_Params.m_AccelerateMultiplier = 3.0f;
		camCtrlCreateInfo.m_Params.m_SmoothStepT = 0.5f;
		m_CameraController = std::make_unique<CameraController>(camCtrlCreateInfo);
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);
		if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			m_ViewRenderProfile.m_EnableScenePreExposure = true;
			const bool registered = m_CameraRig.SetReferenceViews(
				{ CoastalSceneReferenceViews.begin(), CoastalSceneReferenceViews.end() });
			GGLAB_ASSERT_MSG(registered, "Coastal scene reference views must be valid.");
			const bool restored = m_CameraRig.RestoreReferenceView("Retreat_Overview");
			GGLAB_ASSERT_MSG(restored, "Coastal scene must start at its retreat overview reference view.");
		}

		// RenderPipeline
		m_RenderPipeline = CreateRenderPipelineForwardPlus();
	}

	std::string_view DemoPlayground::GetName() const noexcept
	{
		if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			return DesktopCoastalAtriumDemoId;
		}
		return DesktopIslandDemoId;
	}

	void DemoPlayground::BeginPrepare() noexcept
	{
		m_AssetOwnerScope.Reset();
		m_World.GetRegistry().clear();
		if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			m_PendingModels = {
				{ .m_Path = "Assets/Models/GGLabCoastalRetreat/GGLabCoastalRetreat.gltf" },
			};
		}
		else
		{
			m_PendingModels = {
				{ .m_Path = "Assets/Models/GGLabIslandPrototype/GGLabIslandPrototype.gltf" },
			};
		}

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
		if (m_HasEnvironmentOverride) return;
		auto* environmentView = m_Services.m_EnvironmentLighting;
		auto* environmentControl = m_Services.m_EnvironmentLightingControl;
		GGLAB_ASSERT_NOT_NULL(environmentView);
		GGLAB_ASSERT_NOT_NULL(environmentControl);
		// Pending Demos own their World but must not override the active environment.
		m_PreviousEnvironment = environmentView->GetEnvironmentLightingSettings();
		m_HasEnvironmentOverride = true;
		if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			// The Runtime publishes this World's sun, sky and IBL as one completed generation.
			environmentControl->SetIntensity(1.0f);
			environmentControl->SetRotationRadians(0.0f);
			environmentControl->SetSkyboxEnabled(true);
			environmentControl->SetBackgroundMode(EnvironmentBackgroundMode::PhysicalSky);
		}
		else
		{
			environmentControl->SetIntensity(0.0f);
			environmentControl->SetSkyboxEnabled(false);
		}
	}

	void DemoPlayground::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_CameraRig.OnResize(width, height);
	}

	void DemoPlayground::OnExit() noexcept
	{
		if (m_HasEnvironmentOverride)
		{
			auto* environmentControl = m_Services.m_EnvironmentLightingControl;
			GGLAB_ASSERT_NOT_NULL(environmentControl);
			environmentControl->SetIntensity(m_PreviousEnvironment.m_Intensity);
			environmentControl->SetSkyboxEnabled(m_PreviousEnvironment.m_EnableSkybox);
			if (m_Content == PlaygroundContent::CoastalAtrium)
			{
				environmentControl->SetRotationRadians(m_PreviousEnvironment.m_RotationRadians);
				environmentControl->SetBackgroundMode(m_PreviousEnvironment.m_BackgroundMode);
			}
			m_HasEnvironmentOverride = false;
		}
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
		m_World.m_Atmosphere = m_Content == PlaygroundContent::CoastalAtrium
			? std::optional<AtmosphereSettings>(AtmosphereSettings{}) : std::nullopt;
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
			// Retreat's exported photon direction: 23 degree sun elevation, authored meters.
			Vector3 direction = m_Content == PlaygroundContent::CoastalAtrium ?
				Vector3(-0.6840816f, -0.3907311f, 0.6159450f) : Vector3(-0.6f, -1.6f, 0.4f);
			direction.Normalize();
			transComp.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			registry.emplace<components::TransformComponent>(mainLightEntity, transComp);

			components::LightComponent lightComp{};
			lightComp.m_Intensity = 3.0f;
			lightComp.m_Color = Color::White;
			lightComp.m_Type = LightType::Directional;
			lightComp.m_Range = 1000.0f;
			lightComp.m_DirectionalShadowSettings.emplace();
			if (m_Content == PlaygroundContent::CoastalAtrium)
			{
				// TOA illuminance is attenuated by the same atmosphere that lights the sky and IBL.
				lightComp.m_WorldSun = WorldSunSettings{};
			}
			registry.emplace<components::LightComponent>(mainLightEntity, lightComp);
		}
	}
}
