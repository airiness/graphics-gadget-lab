#include "Application/Demo/StartDemo.h"
#include "Core/Input/InputManager.h"
#include "Core/Input/Keyboard.h"
#include "Core/Input/Mouse.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Transform.h"
#include "Core/Time.h"
#include "Graphics/Asset/Loading/AssetLoadProgress.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraController.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/EnvironmentLightingSystem.h"
#include "Graphics/Geometry.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		const StringID StartDemoDebugChannel("Demo.Start.Guides");

		components::MaterialInstanceComponent MakeMaterial(
			std::string_view key, const Color& baseColor, float metallic, float roughness) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = baseColor;
			material.m_Properties.m_MetallicFactor = metallic;
			material.m_Properties.m_RoughnessFactor = roughness;
			return material;
		}
	}

	StartDemo::StartDemo(const DemoCreateInfo& createInfo) noexcept :
		m_Services(createInfo.m_Services)
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "StartDemo requires valid create info.");

		Vector3 cameraForward(0.0f, -0.16f, 1.0f);
		cameraForward.Normalize();
		Camera::CreateInfo cameraCreateInfo{};
		cameraCreateInfo.m_Forward = cameraForward;
		cameraCreateInfo.m_Position = Vector3(0.0f, 2.8f, -8.0f);
		cameraCreateInfo.m_Width = createInfo.m_WindowWidth;
		cameraCreateInfo.m_Height = createInfo.m_WindowHeight;
		cameraCreateInfo.m_Near = 0.1f;
		cameraCreateInfo.m_Far = 500.0f;
		cameraCreateInfo.m_Fov = 55.0f;
		m_Camera = std::make_unique<Camera>(cameraCreateInfo);

		CameraController::CreateInfo controllerCreateInfo{};
		controllerCreateInfo.m_Params.m_MovementSpeed = 7.0f;
		controllerCreateInfo.m_Params.m_MouseSensitivityRadPerCount = 0.0018f;
		controllerCreateInfo.m_Params.m_AccelerateMultiplier = 3.0f;
		controllerCreateInfo.m_Params.m_SmoothStepT = 0.5f;
		m_CameraController = std::make_unique<CameraController>(controllerCreateInfo);
		m_CameraRig.AttachMainCamera(*m_Camera, *m_CameraController);
		m_RenderPipeline = std::make_unique<RenderPipelineForwardPBR>();
	}

	void StartDemo::BeginPrepare() noexcept
	{
		m_World.GetRegistry().clear();
		m_AnimatedSphereEntity = entt::null;
		m_AnimatedCubeEntity = entt::null;
		m_AnimationTime = 0.0f;
		BuildScene();
		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.1f,
			.m_Stage = "Preparing procedural meshes",
			.m_Detail = "ProceduralCube and ProceduralSphere",
		};
	}

	void StartDemo::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}

		LoadingProgressBuilder progress;
		progress.AddCompletedStep(0.1f);
		const std::array meshes = {
			std::pair(ProceduralCubeMeshID, std::string_view("ProceduralCube")),
			std::pair(ProceduralSphereMeshID, std::string_view("ProceduralSphere")),
		};
		for (const auto& [meshId, name] : meshes)
		{
			const Mesh* mesh = m_Services.m_AssetManager->GetMesh(meshId);
			if (!mesh)
			{
				progress.AddStep(0.45f,
					{
						.m_Status = LoadingStatus::Failed,
						.m_Fraction = 0.0f,
						.m_Stage = "Procedural mesh unavailable",
						.m_Detail = name,
					});
				continue;
			}

			progress.AddAssetStep(0.45f,
				GetAssetLoadProgress(mesh->m_State, AssetLoadKind::Mesh, mesh->m_LoadProgress),
				name);
		}

		m_LoadingProgress = progress.Build();
		if (m_LoadingProgress.IsReady())
		{
			m_LoadingProgress.m_Stage = "Start scene ready";
			m_LoadingProgress.m_Detail = "Procedural meshes and GPU resources are ready.";
		}
	}

	void StartDemo::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(), "StartDemo committed before preparation completed.");
	}

	void StartDemo::CancelPrepare() noexcept
	{
		m_World.GetRegistry().clear();
		m_AnimatedSphereEntity = entt::null;
		m_AnimatedCubeEntity = entt::null;
		m_AnimationTime = 0.0f;
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void StartDemo::OnEnter() noexcept
	{
		if (auto* environmentSystem = m_Services.m_Renderer->GetEnvironmentLightingSystem())
		{
			m_PreviousSkyboxEnabled = environmentSystem->GetSettings().m_EnableSkybox;
			m_HasSkyboxOverride = true;
			environmentSystem->SetSkyboxEnabled(false);
		}

		auto* debugDraw = m_Services.m_DebugDraw;
		GGLAB_ASSERT_NOT_NULL(debugDraw);
		debugDraw->SetChannelEnabled(StartDemoDebugChannel, true);
		const DebugDrawStyle guideStyle{
			.m_Color = Color(0.2f, 0.75f, 1.0f, 0.8f),
			.m_Channel = StartDemoDebugChannel,
			.m_DurationSeconds = 3600.0f,
		};
		debugDraw->Grid(Vector3(0.0f, -1.0f, 6.0f), Vector3::UnitY, Vector3::UnitX, 5.0f, 10,
			{
				.m_Color = Color(0.24f, 0.26f, 0.3f, 1.0f),
				.m_Channel = StartDemoDebugChannel,
				.m_DurationSeconds = 3600.0f,
			});
		debugDraw->Axes(
			math::CreateTranslation(Vector3(0.0f, -0.9f, 6.0f)), 1.5f, 0.2f, guideStyle);
		debugDraw->Box(Vector3(-2.2f, 0.0f, 6.0f), Vector3(1.15f), guideStyle);
		debugDraw->Sphere(Vector3(0.0f, 0.0f, 6.0f), 1.2f, guideStyle);
		debugDraw->Capsule(Vector3(2.2f, 0.0f, 6.0f), Vector3::UnitY, 0.9f, 0.55f, guideStyle);
	}

	void StartDemo::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_CameraRig.OnResize(width, height);
	}

	void StartDemo::OnExit() noexcept
	{
		if (m_HasSkyboxOverride)
		{
			if (auto* environmentSystem = m_Services.m_Renderer->GetEnvironmentLightingSystem())
			{
				environmentSystem->SetSkyboxEnabled(m_PreviousSkyboxEnabled);
			}
			m_HasSkyboxOverride = false;
		}

		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->ClearChannel(StartDemoDebugChannel);
		}
	}

	void StartDemo::Update() noexcept
	{
		auto* inputManager = m_Services.m_InputManager;
		auto* time = m_Services.m_Time;
		GGLAB_ASSERT_NOT_NULL(inputManager);
		GGLAB_ASSERT_NOT_NULL(time);

		auto* mouse = inputManager->GetMouse();
		auto* keyboard = inputManager->GetKeyboard();
		const auto mouseCoord = mouse->GetMouseCoord();
		CameraInput input{};
		input.m_Front = keyboard->IsKeyHeld(KeyCode::W);
		input.m_Back = keyboard->IsKeyHeld(KeyCode::S);
		input.m_Left = keyboard->IsKeyHeld(KeyCode::A);
		input.m_Right = keyboard->IsKeyHeld(KeyCode::D);
		input.m_Up = keyboard->IsKeyHeld(KeyCode::Q);
		input.m_Down = keyboard->IsKeyHeld(KeyCode::E);
		input.m_Accelerate = keyboard->IsKeyHeld(KeyCode::LeftShift);
		input.m_IsMouseRelative = mouse->GetMouseMode() == Mouse::MouseMode::Relative;
		input.m_MouseDelta = mouseCoord;

		Camera& camera = m_CameraRig.GetActiveCamera();
		CameraController& controller = m_CameraRig.GetActiveCameraController();
		const float deltaTime = static_cast<float>(time->GetDeltaTime());
		controller.Update(camera, input, deltaTime);

		m_AnimationTime += std::max(deltaTime, 0.0f);
		auto& registry = m_World.GetRegistry();
		if (registry.valid(m_AnimatedSphereEntity))
		{
			auto& transform = registry.get<components::TransformComponent>(m_AnimatedSphereEntity);
			transform.m_Position.m_Y = 0.2f + std::sin(m_AnimationTime * 1.5f) * 0.35f;
		}

		if (registry.valid(m_AnimatedCubeEntity))
		{
			auto& transform = registry.get<components::TransformComponent>(m_AnimatedCubeEntity);
			transform.m_Rotation = math::CreateFromYawPitchRoll(
				0.45f + m_AnimationTime * 0.7f, std::sin(m_AnimationTime * 0.8f) * 0.12f, 0.15f);
		}
		camera.Update();
	}

	void StartDemo::BuildScene() noexcept
	{
		auto* assetManager = m_Services.m_AssetManager;
		auto* renderer = m_Services.m_Renderer;
		GGLAB_ASSERT_NOT_NULL(assetManager);
		GGLAB_ASSERT_NOT_NULL(renderer);
		auto* samplerRegistry = renderer->GetSamplerRegistry();

		components::TransformComponent platformTransform{};
		platformTransform.m_Position = Vector3(0.0f, -1.25f, 6.0f);
		platformTransform.m_Scale = Vector3(4.0f, 0.12f, 3.0f);
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = assetManager,
			.m_SamplerRegistry = samplerRegistry,
			.m_World = &m_World,
			.m_Transform = platformTransform,
			.m_MaterialInstance = MakeMaterial("gglab.demo.start.material.platform",
				Color(0.18f, 0.2f, 0.24f, 1.0f), 0.05f, 0.75f),
			}));

		components::TransformComponent cubeTransform{};
		cubeTransform.m_Position = Vector3(-2.2f, 0.0f, 6.0f);
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = assetManager,
			.m_SamplerRegistry = samplerRegistry,
			.m_World = &m_World,
			.m_Transform = cubeTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.demo.start.material.cube", Color(0.08f, 0.42f, 0.9f, 1.0f), 0.15f, 0.28f),
			}));

		components::TransformComponent sphereTransform{};
		sphereTransform.m_Position = Vector3(0.0f, 0.0f, 6.0f);
		m_AnimatedSphereEntity = primitive::Sphere::Create({
			.m_AssetManager = assetManager,
			.m_SamplerRegistry = samplerRegistry,
			.m_World = &m_World,
			.m_Transform = sphereTransform,
			.m_MaterialInstance = MakeMaterial(
				"gglab.demo.start.material.sphere", Color(0.95f, 0.38f, 0.08f, 1.0f), 0.7f, 0.2f),
			});

		components::TransformComponent tallCubeTransform{};
		tallCubeTransform.m_Position = Vector3(2.2f, 0.0f, 6.0f);
		tallCubeTransform.m_Rotation = math::CreateFromYawPitchRoll(0.45f, 0.0f, 0.15f);
		tallCubeTransform.m_Scale = Vector3(0.65f, 1.15f, 0.65f);
		m_AnimatedCubeEntity = primitive::Cube::Create({
			.m_AssetManager = assetManager,
			.m_SamplerRegistry = samplerRegistry,
			.m_World = &m_World,
			.m_Transform = tallCubeTransform,
			.m_MaterialInstance = MakeMaterial("gglab.demo.start.material.tall_cube",
				Color(0.2f, 0.82f, 0.48f, 1.0f), 0.3f, 0.42f),
			});

		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();
		components::TransformComponent lightTransform{};
		Vector3 lightDirection(-0.45f, -0.82f, 0.35f);
		lightDirection.Normalize();
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, lightDirection);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color(1.0f, 0.94f, 0.82f, 1.0f);
		light.m_Intensity = 3.5f;
		light.m_Range = 1000.0f;
		light.m_DirectionalShadowSettings.emplace();
		registry.emplace<components::LightComponent>(lightEntity, light);
	}
}
