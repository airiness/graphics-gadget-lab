#include "Core/Precompiled.h"
#include "Core/Math/Quaternion.h"
#include "Core/Math/Transform.h"
#include "Application/Lab/Sessions/HelloLabSession.h"
#include "Scene/Components.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"

namespace gglab
{
	namespace
	{
		const LabParameterId EnableCameraInputId("hello.camera.enable_input");
		const LabParameterId CameraFovId("hello.camera.fov");
		const LabParameterId ModelPositionId("hello.model.position");
		const LabParameterId ModelScaleId("hello.model.scale");
		const LabParameterId MaterialBaseColorId("hello.material.base_color");
		const LabParameterId MaterialMetallicId("hello.material.metallic");
		const LabParameterId MaterialRoughnessId("hello.material.roughness");
		const LabParameterId LightColorId("hello.light.color");
		const LabParameterId LightIntensityId("hello.light.intensity");
		const LabParameterId LightDirectionId("hello.light.direction");
		const StringID DebugDrawShapeChannel("DebugDraw.ShapeLibrary");
	}

	void HelloLabSession::OnEnter() noexcept
	{
		auto* debugDraw = m_Services.m_DebugDraw;
		GGLAB_ASSERT_NOT_NULL(debugDraw);
		debugDraw->SetChannelEnabled(DebugDrawShapeChannel, true);
		const DebugDrawStyle wireStyle{
			.m_Color = Color::Cyan,
			.m_Channel = DebugDrawShapeChannel,
			.m_DurationSeconds = 3600.0f,
		};
		DebugDrawStyle solidStyle = wireStyle;
		solidStyle.m_Color = Color(1.0f, 0.35f, 0.05f, 0.35f);
		solidStyle.m_FillMode = DebugDrawFillMode::Solid;
		DebugDrawStyle cullingWireStyle = wireStyle;
		cullingWireStyle.m_Color = Color::LightGray;
		cullingWireStyle.m_CullingMode = DebugDrawCullingMode::MainViewFrustum;
		DebugDrawStyle cullingSolidStyle = solidStyle;
		cullingSolidStyle.m_Color = Color(0.15f, 0.85f, 0.55f, 0.45f);
		cullingSolidStyle.m_CullingMode = DebugDrawCullingMode::MainViewFrustum;

		debugDraw->Circle(Vector3(0.0f, -1.25f, 8.0f), Vector3::UnitY, 4.0f,
			{ .m_Color = Color::DarkGray, .m_Channel = DebugDrawShapeChannel,
				.m_DurationSeconds = 3600.0f });
		debugDraw->Sphere(Vector3(-3.0f, 0.0f, 8.0f), 1.0f, wireStyle);
		debugDraw->Sphere(Vector3(-1.0f, 0.0f, 8.0f), 1.0f, solidStyle);
		debugDraw->Cone(Vector3(1.0f, 1.0f, 8.0f), Vector3::Down, 2.0f, 0.8f, wireStyle);
		debugDraw->Cylinder(Vector3(3.0f, 0.0f, 8.0f), Vector3::UnitY, 2.0f, 0.8f, solidStyle);
		debugDraw->Capsule(Vector3(-2.0f, 0.0f, 11.0f), Vector3::UnitY, 1.0f, 0.65f, wireStyle);
		debugDraw->Box(Vector3(0.0f, 0.0f, 11.0f), Vector3(0.8f), solidStyle);
		debugDraw->Obb(
			math::CreateTransformMatrix(
				Vector3::One,
				math::CreateFromYawPitchRoll(0.45f, 0.25f, 0.15f),
				Vector3(2.0f, 0.0f, 11.0f)),
			Vector3(0.9f, 0.6f, 0.75f),
			wireStyle);

		constexpr std::array<Vector3, 7> cullingProbePositions = {
			Vector3(-14.0f, 0.0f, 18.0f),
			Vector3(-7.0f, 0.0f, 18.0f),
			Vector3(0.0f, 0.0f, 18.0f),
			Vector3(7.0f, 0.0f, 18.0f),
			Vector3(14.0f, 0.0f, 18.0f),
			Vector3(0.0f, 8.0f, 20.0f),
			Vector3(0.0f, 0.0f, -8.0f),
		};
		for (size_t index = 0; index < cullingProbePositions.size(); ++index)
		{
			const Vector3& position = cullingProbePositions[index];
			if ((index % 2) == 0)
			{
				debugDraw->Sphere(position, 0.85f, cullingWireStyle);
			}
			else
			{
				debugDraw->Box(position, Vector3(0.85f), cullingSolidStyle);
			}
		}

		const std::array<Vector3, 8> frustumCorners = {
			Vector3(-0.4f, 0.4f, 12.5f), Vector3(0.4f, 0.4f, 12.5f),
			Vector3(0.4f, -0.4f, 12.5f), Vector3(-0.4f, -0.4f, 12.5f),
			Vector3(-1.4f, 1.2f, 14.0f), Vector3(1.4f, 1.2f, 14.0f),
			Vector3(1.4f, -1.2f, 14.0f), Vector3(-1.4f, -1.2f, 14.0f),
		};
		debugDraw->Frustum(frustumCorners, wireStyle);
	}

	void HelloLabSession::OnExit() noexcept
	{
		if (auto* debugDraw = m_Services.m_DebugDraw)
		{
			debugDraw->ClearChannel(DebugDrawShapeChannel);
		}
	}

	HelloLabSession::HelloLabSession(const LabSessionCreateInfo& createInfo) noexcept :
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
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.1f),
			.m_MaxValue = LabValue(10.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MaterialBaseColorId,
			.m_Name = "Base Color",
			.m_Group = "Material",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = Color(0.8f, 0.15f, 0.05f, 1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MaterialMetallicId,
			.m_Name = "Metallic",
			.m_Group = "Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.2f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MaterialRoughnessId,
			.m_Name = "Roughness",
			.m_Group = "Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.35f,
			.m_MinValue = LabValue(0.04f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = LightColorId,
			.m_Name = "Color",
			.m_Group = "Lighting",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = Color::White,
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

		auto* debugDraw = m_Services.m_DebugDraw;
		GGLAB_ASSERT_NOT_NULL(debugDraw);
		const Vector3 modelPosition = GetParameters().Get(
			ModelPositionId, Vector3(0.0f, 0.0f, 5.0f));
		const float modelScale = GetParameters().Get(ModelScaleId, 1.0f);
		debugDraw->Axes(math::CreateTranslation(modelPosition), modelScale * 1.5f,
			modelScale * 0.25f,
			{ .m_FillMode = DebugDrawFillMode::Solid, .m_Channel = DebugDrawShapeChannel });
		debugDraw->Aabb(math::Aabb(modelPosition, Vector3::One * modelScale), {
			.m_Color = Color::Yellow,
			.m_Channel = DebugDrawShapeChannel,
		});
		debugDraw->Arrow(
			modelPosition,
			modelPosition + Vector3::UnitY * modelScale * 2.5f,
			modelScale * 0.4f,
			{
				.m_Color = Color::Cyan,
				.m_DepthMode = DebugDrawDepthMode::Always,
				.m_FillMode = DebugDrawFillMode::Solid,
				.m_Channel = DebugDrawShapeChannel,
			});
		debugDraw->Point(Vector3(24.0f, 24.0f, 0.0f), 8.0f, {
			.m_Color = Color::Magenta,
			.m_Space = DebugDrawSpace::Screen,
			.m_DepthMode = DebugDrawDepthMode::Always,
			.m_Channel = DebugDrawShapeChannel,
		});
		debugDraw->Circle(
			Vector3(56.0f, 24.0f, 0.0f), Vector3::UnitZ, 8.0f,
			{
				.m_Color = Color(0.2f, 0.8f, 1.0f, 0.65f),
				.m_Space = DebugDrawSpace::Screen,
				.m_DepthMode = DebugDrawDepthMode::Always,
				.m_FillMode = DebugDrawFillMode::Solid,
				.m_Channel = DebugDrawShapeChannel,
			});
	}

	void HelloLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		GetCamera().SetFov(parameters.Get(CameraFovId, 60.0f));

		const Color lightColor = parameters.Get(LightColorId, Color::White);
		const float lightIntensity = parameters.Get(LightIntensityId, 3.0f);
		const Color baseColor = parameters.Get(
			MaterialBaseColorId,
			Color(0.8f, 0.15f, 0.05f, 1.0f));
		const float metallic = parameters.Get(MaterialMetallicId, 0.2f);
		const float roughness = parameters.Get(MaterialRoughnessId, 0.35f);
		auto materialView = m_World.GetRegistry().view<components::MaterialInstanceComponent>();
		for (const entt::entity entity : materialView)
		{
			auto& material = materialView.get<components::MaterialInstanceComponent>(entity);
			material.m_Properties.m_BaseColor = baseColor;
			material.m_Properties.m_MetallicFactor = metallic;
			material.m_Properties.m_RoughnessFactor = roughness;
		}

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
		const float modelScale = parameters.Get(ModelScaleId, 1.0f);

		components::TransformComponent modelTransform{};
		modelTransform.m_Position = modelPosition;
		modelTransform.m_Scale = Vector3::One * modelScale;
		components::MaterialInstanceComponent materialInstance{};
		materialInstance.m_Key = RuntimeMaterialKey("gglab.lab.hello.material.cube");
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = modelTransform,
			.m_MaterialInstance = materialInstance,
		}));

		constexpr std::array<Vector3, 9> cullingCandidatePositions = {
			Vector3(-18.0f, 0.0f, 22.0f),
			Vector3(-9.0f, 0.0f, 22.0f),
			Vector3(0.0f, 0.0f, 22.0f),
			Vector3(9.0f, 0.0f, 22.0f),
			Vector3(18.0f, 0.0f, 22.0f),
			Vector3(0.0f, 9.0f, 24.0f),
			Vector3(0.0f, -5.0f, 24.0f),
			Vector3(0.0f, 0.0f, -12.0f),
			Vector3(0.0f, 0.0f, 80.0f),
		};
		for (size_t index = 0; index < cullingCandidatePositions.size(); ++index)
		{
			components::TransformComponent candidateTransform{};
			candidateTransform.m_Position = cullingCandidatePositions[index];
			candidateTransform.m_Scale = Vector3::One * 0.8f;
			if ((index % 2) == 0)
			{
				GGLAB_UNUSED(primitive::Cube::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
					.m_World = &m_World,
					.m_Transform = candidateTransform,
					.m_MaterialInstance = materialInstance,
				}));
			}
			else
			{
				GGLAB_UNUSED(primitive::Sphere::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
					.m_World = &m_World,
					.m_Transform = candidateTransform,
					.m_MaterialInstance = materialInstance,
				}));
			}
		}

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
			direction = Vector3(-0.406f, -0.906f, 0.123f);
			break;
		}
		direction.Normalize();
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
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

	std::unique_ptr<LabSessionBase> HelloLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<HelloLabSession>(createInfo);
	}
}
