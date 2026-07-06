#include "Core/Precompiled.h"
#include "Application/Demo/DemoPlayground.h"
#include "Core/Time.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/Mouse.h"
#include "Core/Input/Keyboard.h"
#include "Scene/Components.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/AssetManager.h"

namespace gglab
{
	DemoPlayground::DemoPlayground(const DemoCreateInfo& createInfo) noexcept :
		m_Services(createInfo.m_Services)
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

		// RenderPipeline
		m_RenderPipeline = std::make_unique<RenderPipelineForwardPBR>();

		// Scene initialize
		InitializeScene();
	}

	void DemoPlayground::OnEnter() noexcept
	{
	}

	void DemoPlayground::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_Camera->OnResize(width, height);
	}

	void DemoPlayground::OnExit() noexcept
	{
	}

	void DemoPlayground::Update() noexcept
	{
		auto* inputManager = m_Services.m_InputManager;
		auto* time = m_Services.m_Time;
		GGLAB_ASSERT_NOT_NULL(inputManager);
		GGLAB_ASSERT_NOT_NULL(time);
		auto* mouse = inputManager->GetMouse();
		auto* keyboard = inputManager->GetKeyboard();
		const auto mouseCoord = mouse->GetMouseCoord();
		const auto deltaTime = static_cast<float>(time->GetDeltaTime());

		CameraInput camInput{};
		camInput.m_Front = keyboard->IsKeyHeld(KeyCode::W);
		camInput.m_Back = keyboard->IsKeyHeld(KeyCode::S);
		camInput.m_Left = keyboard->IsKeyHeld(KeyCode::A);
		camInput.m_Right = keyboard->IsKeyHeld(KeyCode::D);
		camInput.m_Up = keyboard->IsKeyHeld(KeyCode::Q);
		camInput.m_Down = keyboard->IsKeyHeld(KeyCode::E);
		camInput.m_Accelerate = keyboard->IsKeyHeld(KeyCode::LeftShift);
		camInput.m_IsMouseRelative = mouse->GetMouseMode() == Mouse::MouseMode::Relative;
		camInput.m_MouseDelta = mouseCoord;

		m_CameraController->Update(*m_Camera, camInput, deltaTime);

		m_Camera->Update();
	}

	void DemoPlayground::InitializeScene() noexcept
	{
		auto* assetManager = m_Services.m_AssetManager;
		GGLAB_ASSERT_NOT_NULL(assetManager);
		auto& registry = m_World.GetRegistry();

		auto createEntityWithModel = [&](
			const std::filesystem::path& modelPath,
			const Vector3& position,
			const Vector3& rotation,
			const Vector3& scale)
			{
				auto modelId = assetManager->LoadModel(modelPath);
				auto entity = registry.create();
				components::TransformComponent transformComp{};
				transformComp.m_Position = position;
				transformComp.m_Rotation = Quaternion::CreateFromYawPitchRoll(
					utils::ToRadians(rotation.y),
					utils::ToRadians(rotation.x),
					utils::ToRadians(rotation.z));
				transformComp.m_Scale = scale;
				registry.emplace<components::TransformComponent>(entity, transformComp);

				components::ModelComponent modelComp{};
				modelComp.m_ModelId = modelId;
				registry.emplace<components::ModelComponent>(entity, modelComp);
			};

		// Test Sponza
		createEntityWithModel(
			"Assets/Models/Sponza/Sponza.gltf",
			Vector3::Zero,
			Vector3::Zero,
			Vector3::One);

		// Flight helmet
		createEntityWithModel(
			"Assets/Models/FlightHelmet/FlightHelmet.gltf",
			Vector3(0.0f, 0.0f, 0.0f),
			Vector3::Zero,
			Vector3::One);

		// Main Light
		{
			auto mainLightEntity = registry.create();

			components::TransformComponent transComp{};
			Vector3 direction = Vector3(-0.406f, -0.906f, -0.123f);
			direction.Normalize();
			Quaternion::FromToRotation(-Vector3::UnitZ, direction, transComp.m_Rotation);
			registry.emplace<components::TransformComponent>(mainLightEntity, transComp);

			components::LightComponent lightComp{};
			lightComp.m_Intensity = 3.0f;
			lightComp.m_Color = color::White;
			lightComp.m_Type = LightType::Directional;
			lightComp.m_Range = 1000.0f;
			lightComp.m_DirectionalShadowSettings.emplace();
			registry.emplace<components::LightComponent>(mainLightEntity, lightComp);
		}
	}
}
