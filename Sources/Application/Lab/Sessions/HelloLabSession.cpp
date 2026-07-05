#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/HelloLabSession.h"
#include "Scene/Components.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		const LabParameterId EnableCameraInputId("hello.camera.enable_input");
		const LabParameterId CameraFovId("hello.camera.fov");
		const LabParameterId ModelPositionId("hello.model.position");
		const LabParameterId ModelScaleId("hello.model.scale");
		const LabParameterId LightColorId("hello.light.color");
		const LabParameterId LightIntensityId("hello.light.intensity");
		const LabParameterId LightDirectionId("hello.light.direction");
	}

	HelloLabSession::HelloLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSession(
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
			.m_DefaultValue = 60.0f,
			.m_MinValue = LabValue(30.0f),
			.m_MaxValue = LabValue(100.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ModelPositionId,
			.m_Name = "Position",
			.m_Group = "Model",
			.m_Type = LabParameterType::Vector3,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = Vector3(0.0f, 0.0f, 5.0f),
			.m_MinValue = LabValue(Vector3(-20.0f)),
			.m_MaxValue = LabValue(Vector3(20.0f)),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ModelScaleId,
			.m_Name = "Scale",
			.m_Group = "Model",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = 5.0f,
			.m_MinValue = LabValue(0.1f),
			.m_MaxValue = LabValue(20.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = LightColorId,
			.m_Name = "Color",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = color::White,
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
		GGLAB_UNUSED(parameters.Add({
			.m_Id = LightDirectionId,
			.m_Name = "Direction",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = int32_t(1),
			.m_EnumItems = {
				{ .m_Value = 0, .m_Name = "Front" },
				{ .m_Value = 1, .m_Name = "Diagonal" },
				{ .m_Value = 2, .m_Name = "Top" },
			},
		}));

		ApplyImmediateParameters();
		RebuildScene();
	}

	void HelloLabSession::Update(float deltaTime) noexcept
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

	void HelloLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		GetCamera().SetFov(parameters.Get(CameraFovId, 60.0f));

		const Color lightColor = parameters.Get(LightColorId, color::White);
		const float lightIntensity = parameters.Get(LightIntensityId, 3.0f);
		auto view = m_World.GetRegistry().view<components::LightComponent>();
		for (const entt::entity entity : view)
		{
			auto& light = view.get<components::LightComponent>(entity);
			light.m_Color = lightColor;
			light.m_Intensity = lightIntensity;
		}
	}

	void HelloLabSession::RebuildScene() noexcept
	{
		auto& registry = m_World.GetRegistry();
		registry.clear();

		const auto& parameters = GetParameters();
		const Vector3 modelPosition = parameters.Get(
			ModelPositionId,
			Vector3(0.0f, 0.0f, 5.0f));
		const float modelScale = parameters.Get(ModelScaleId, 5.0f);

		const entt::entity modelEntity = registry.create();
		components::TransformComponent transform{};
		transform.m_Position = modelPosition;
		transform.m_Scale = Vector3::One * modelScale;
		registry.emplace<components::TransformComponent>(modelEntity, transform);

		components::ModelComponent model{};
		model.m_ModelId = m_Services.m_AssetManager->LoadModel(
			"Assets/Models/FlightHelmet/FlightHelmet.gltf");
		registry.emplace<components::ModelComponent>(modelEntity, model);

		const entt::entity lightEntity = registry.create();
		components::TransformComponent lightTransform{};
		Vector3 direction = Vector3(0.0f, 0.0f, -1.0f);
		switch (parameters.Get(LightDirectionId, int32_t(1)))
		{
		case 0:
			direction = Vector3(0.0f, 0.0f, -1.0f);
			break;
		case 2:
			direction = Vector3(0.0f, -1.0f, 0.0f);
			break;
		default:
			direction = Vector3(-0.406f, -0.906f, -0.123f);
			break;
		}
		direction.Normalize();
		Quaternion::FromToRotation(-Vector3::UnitZ, direction, lightTransform.m_Rotation);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Range = 1000.0f;
		light.m_DirectionalShadowSettings.emplace();
		registry.emplace<components::LightComponent>(lightEntity, light);
		ApplyImmediateParameters();
	}

	LabId HelloLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.hello");
	}

	LabDescriptor HelloLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Hello Lab",
			.m_Category = "Foundation",
			.m_Description = "A compact scene for validating Lab parameters and lifecycle commands.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSession> HelloLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<HelloLabSession>(createInfo);
	}
}
