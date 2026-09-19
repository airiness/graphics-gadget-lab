#include "Application/Demo/DemoPlayground.h"
#include "Application/Demo/CoastalAtriumReferenceViews.h"
#include "ApplicationCameraInput.h"
#include "Application/Content/DesktopApplicationContent.h"
#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Time.h"
#include "GGLabRuntime/Scene/Components.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraController.h"
#include "GGLabRuntime/Graphics/CameraReferenceView.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "GGLabRuntime/Graphics/Asset/AssetLoadProgress.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"

namespace gglab
{
	DemoPlayground::DemoPlayground(const DemoCreateInfo& createInfo,
		PlaygroundContent content) noexcept :
		m_Services(createInfo.m_Services),
		m_Content(content),
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
		if (m_Content != PlaygroundContent::Sponza)
		{
			// Blender (X, Y, Z) maps to runtime (X, Z, Y); preserve authored meters.
			camCreateInfo.m_Position = Vector3(17.0f, 16.0f, -23.0f);
			camCreateInfo.m_Forward = Vector3(0.0f, 0.8f, 0.0f) - camCreateInfo.m_Position;
			camCreateInfo.m_Forward.Normalize();
			camCreateInfo.m_Far = 100.0f;
			camCreateInfo.m_Fov = math::ToDegrees(0.4426289085f);
			camCreateInfo.m_ExposureCompensationEV = 0.0f;
			m_ViewRenderProfile.m_TemporalAA.m_Enabled = false;
			m_ViewRenderProfile.m_Lighting.m_GTAO.m_Enabled = false;
			m_ViewRenderProfile.m_PostProcess.m_Bloom.m_Enabled = false;
		}
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
			const bool registered = m_CameraRig.SetReferenceViews(
				{ CoastalAtriumReferenceViews.begin(), CoastalAtriumReferenceViews.end() });
			GGLAB_ASSERT_MSG(registered, "Coastal atrium reference views must be valid.");
			const bool restored = m_CameraRig.RestoreReferenceView(CoastalAtriumReferenceViews.front().m_Id);
			GGLAB_ASSERT_MSG(restored, "Coastal atrium must start at its courtyard reference view.");
		}
		else if (m_Content == PlaygroundContent::TextureContract)
		{
			const bool registered = m_CameraRig.SetReferenceViews({ CameraReferenceView{
				.m_Id = "CAM_TextureContract",
				.m_Name = "Texture Contract",
				.m_Purpose = "Top: UV and sRGB. Middle: tangent normals. Bottom: metallic/roughness.",
				.m_Position = { 0.0f, 2.3f, -12.5f },
				.m_Target = { 0.0f, 2.3f, 0.0f },
				.m_VerticalFovDegrees = math::ToDegrees(0.6509917105f),
				.m_FarPlane = 50.0f,
			} });
			GGLAB_ASSERT_MSG(registered, "Texture contract reference view must be valid.");
			const bool restored = m_CameraRig.RestoreReferenceView("CAM_TextureContract");
			GGLAB_ASSERT_MSG(restored, "Texture contract must start at its reference view.");
		}

		// RenderPipeline
		m_RenderPipeline = CreateRenderPipelineForwardPBR();
	}

	std::string_view DemoPlayground::GetName() const noexcept
	{
		if (m_Content == PlaygroundContent::TextureContract)
		{
			return DesktopTextureContractDemoId;
		}
		if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			return DesktopCoastalAtriumDemoId;
		}
		return m_Content == PlaygroundContent::Island ?
			DesktopIslandDemoId : DesktopPlaygroundDemoId;
	}

	void DemoPlayground::BeginPrepare() noexcept
	{
		m_AssetOwnerScope.Reset();
		m_World.GetRegistry().clear();
		if (m_Content == PlaygroundContent::TextureContract)
		{
			m_PendingModels = {
				{ .m_Path = "Assets/Models/GGLabTextureContract/GGLabTextureContract.gltf" },
			};
		}
		else if (m_Content == PlaygroundContent::CoastalAtrium)
		{
			m_PendingModels = {
				{ .m_Path = "Assets/Models/GGLabCoastalAtrium/GGLabCoastalAtrium.gltf" },
			};
		}
		else if (m_Content == PlaygroundContent::Island)
		{
			m_PendingModels = {
				{ .m_Path = "Assets/Models/GGLabIslandPrototype/GGLabIslandPrototype.gltf" },
			};
		}
		else
		{
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
		if (m_Content != PlaygroundContent::Sponza)
		{
			auto* environmentView = m_Services.m_EnvironmentLighting;
			auto* environmentControl = m_Services.m_EnvironmentLightingControl;
			GGLAB_ASSERT_NOT_NULL(environmentView);
			GGLAB_ASSERT_NOT_NULL(environmentControl);
			const auto previous = environmentView->GetEnvironmentLightingSettings();
			m_PreviousEnvironmentIntensity = previous.m_Intensity;
			m_PreviousSkyboxEnabled = previous.m_EnableSkybox;
			m_HasEnvironmentOverride = true;
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
			environmentControl->SetIntensity(m_PreviousEnvironmentIntensity);
			environmentControl->SetSkyboxEnabled(m_PreviousSkyboxEnabled);
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
			if (m_Content == PlaygroundContent::Island)
			{
				direction = Vector3(-0.6f, -1.6f, 0.4f);
			}
			else if (m_Content == PlaygroundContent::CoastalAtrium)
			{
				direction = Vector3(-1.0f, -0.85f, 0.35f);
			}
			else if (m_Content == PlaygroundContent::TextureContract)
			{
				direction = Vector3(-0.45f, -0.65f, 1.0f);
			}
			direction.Normalize();
			transComp.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			registry.emplace<components::TransformComponent>(mainLightEntity, transComp);

			components::LightComponent lightComp{};
			lightComp.m_Intensity = 3.0f;
			lightComp.m_Color = Color::White;
			lightComp.m_Type = LightType::Directional;
			lightComp.m_Range = 1000.0f;
			// The texture board isolates material inputs from shadow filtering artifacts.
			if (m_Content != PlaygroundContent::TextureContract)
			{
				lightComp.m_DirectionalShadowSettings.emplace();
			}
			registry.emplace<components::LightComponent>(mainLightEntity, lightComp);
		}
	}
}
