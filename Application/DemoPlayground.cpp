#include "Precompiled.h"
#include "DemoPlayground.h"
#include "Camera.h"
#include "Application.h"
#include "AssetManager.h"
#include "Components.h"
#include "RenderPipelineForwardPBR.h"

namespace gglab
{
	DemoPlayground::DemoPlayground() noexcept
	{
		auto* app = Application::GetInstance();

		// Camera
		Camera::CreateInfo info{};
		info.m_Position = Vector3(-100.0f, 128.0f, 30.0f);
		info.m_Width = app->GetWindowWidth();
		info.m_Height = app->GetWindowHeight();
		info.m_Near = 0.1f;
		info.m_Far = 10000.0f;
		info.m_Fov = 60.0f;
		m_Camera = std::make_unique<Camera>(info);

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
		m_Camera->Update();
	}

	void DemoPlayground::InitializeScene() noexcept
	{
		auto* assetManager = Application::GetInstance()->GetAssetManager();
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
			Vector3::One * 0.1f);

		// Flight helmet
		createEntityWithModel(
			"Assets/Models/FlightHelmet/FlightHelmet.gltf",
			Vector3(10.0f, 20.0f, 0.0f),
			Vector3::Zero,
			Vector3::One);

		// Alpha Blend Model Test
		createEntityWithModel(
			"Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf",
			Vector3(-10.0f, 20.0f, 0.0f),
			Vector3::Zero,
			Vector3::One);

		// Main Light
		{
			auto mainLightEntity = registry.create();

			components::TransformComponent transComp{};
			Vector3 direction = -Vector3::One;
			direction.Normalize();
			Quaternion::FromToRotation(-Vector3::UnitZ, direction, transComp.m_Rotation);
			registry.emplace<components::TransformComponent>(mainLightEntity, transComp);

			components::LightComponent lightComp{};
			lightComp.m_Intensity = 1.0f;
			lightComp.m_Color = color::White;
			lightComp.m_Type = LightType::Directional;
			lightComp.m_Range = 1000.0f;
			registry.emplace<components::LightComponent>(mainLightEntity, lightComp);
		}
	}
}