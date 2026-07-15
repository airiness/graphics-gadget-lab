#include "Core/Precompiled.h"
#include "Core/Math/Quaternion.h"
#include "Application/Lab/Sessions/PostProcessLabSession.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		const LabParameterId EnableCameraInputId("post_process.camera.enable_input");
		const LabParameterId ExposureEvId("post_process.exposure.ev");
		const LabParameterId EmissiveIntensityId("post_process.scene.emissive_intensity");
		const LabParameterId BloomEnabledId("post_process.bloom.enabled");
		const LabParameterId BloomThresholdId("post_process.bloom.threshold");
		const LabParameterId BloomSoftKneeId("post_process.bloom.soft_knee");
		const LabParameterId BloomIntensityId("post_process.bloom.intensity");
		const LabParameterId BloomScatterId("post_process.bloom.scatter");
		const LabParameterId BloomLevelsId("post_process.bloom.levels");

		struct BloomEmitterComponent
		{
			Color m_Color = Color::White;
			float m_IntensityScale = 1.0f;
		};
	}

	PostProcessLabSession::PostProcessLabSession(
		const LabSessionCreateInfo& createInfo) noexcept :
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
			.m_Id = ExposureEvId,
			.m_Name = "Exposure Compensation EV",
			.m_Group = "Exposure",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.0f,
			.m_MinValue = LabValue(-6.0f),
			.m_MaxValue = LabValue(6.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EmissiveIntensityId,
			.m_Name = "Emitter Intensity",
			.m_Group = "HDR Scene",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 12.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(64.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomEnabledId,
			.m_Name = "Enabled",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomThresholdId,
			.m_Name = "Threshold",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(16.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomSoftKneeId,
			.m_Name = "Soft Knee",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.5f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomIntensityId,
			.m_Name = "Intensity",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.08f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(2.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomScatterId,
			.m_Name = "Scatter",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.7f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BloomLevelsId,
			.m_Name = "Pyramid Levels",
			.m_Group = "Bloom",
			.m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(6),
			.m_MinValue = LabValue(uint32_t(1)),
			.m_MaxValue = LabValue(uint32_t(8)),
		}));

		ApplyImmediateParameters();
	}

	void PostProcessLabSession::BeginPrepare() noexcept
	{
		m_AssetPreparation.Reset();
		BuildScene();
		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.35f);
		m_AssetPreparation.TrackModel(ProceduralSphereModelID, "ProceduralSphere", 0.65f);
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager,
			"Preparing Post Process Lab");
	}

	void PostProcessLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager,
			"Preparing Post Process Lab");
	}

	void PostProcessLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(
			m_LoadingProgress.IsReady(),
			"Post Process Lab committed before its procedural models were ready.");
	}

	void PostProcessLabSession::CancelPrepare() noexcept
	{
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void PostProcessLabSession::Update(float deltaTime) noexcept
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

	void PostProcessLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		GetCamera().SetExposureCompensationEV(parameters.Get(ExposureEvId, 0.0f));

		auto& bloom = GetMutableViewRenderProfile().m_PostProcess.m_Bloom;
		bloom.m_Enabled = parameters.Get(BloomEnabledId, true);
		bloom.m_Threshold = parameters.Get(BloomThresholdId, 1.0f);
		bloom.m_SoftKnee = parameters.Get(BloomSoftKneeId, 0.5f);
		bloom.m_Intensity = parameters.Get(BloomIntensityId, 0.08f);
		bloom.m_Scatter = parameters.Get(BloomScatterId, 0.7f);
		bloom.m_MaxLevels = parameters.Get(BloomLevelsId, uint32_t(6));

		const float baseIntensity = parameters.Get(EmissiveIntensityId, 12.0f);
		auto emitterView = m_World.GetRegistry().view<
			BloomEmitterComponent,
			components::MaterialInstanceComponent>();
		for (const entt::entity entity : emitterView)
		{
			const auto& emitter = emitterView.get<BloomEmitterComponent>(entity);
			auto& material = emitterView.get<components::MaterialInstanceComponent>(entity);
			const float intensity = baseIntensity * emitter.m_IntensityScale;
			material.m_Properties.m_EmissiveColor = Color(
				emitter.m_Color.m_R * intensity,
				emitter.m_Color.m_G * intensity,
				emitter.m_Color.m_B * intensity,
				1.0f);
		}
	}

	void PostProcessLabSession::RebuildScene() noexcept
	{
		BuildScene();
	}

	void PostProcessLabSession::OnParametersRestoredForPrepare(
		LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void PostProcessLabSession::BuildScene() noexcept
	{
		auto& registry = m_World.GetRegistry();
		registry.clear();
		ApplyCameraPreset();

		components::TransformComponent floorTransform{};
		floorTransform.m_Position = Vector3(0.0f, -1.25f, 6.0f);
		floorTransform.m_Scale = Vector3(6.5f, 0.2f, 4.5f);
		components::MaterialInstanceComponent floorMaterial{};
		floorMaterial.m_Key = RuntimeMaterialKey("gglab.lab.post_process.material.floor");
		floorMaterial.m_Properties.m_BaseColor = Color(0.035f, 0.04f, 0.05f, 1.0f);
		floorMaterial.m_Properties.m_RoughnessFactor = 0.32f;
		floorMaterial.m_Properties.m_MetallicFactor = 0.15f;
		GGLAB_UNUSED(primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = floorTransform,
			.m_MaterialInstance = floorMaterial,
		}));

		constexpr std::array<Vector3, 5> positions = {
			Vector3(-4.2f, 0.3f, 5.5f),
			Vector3(-2.1f, 0.6f, 5.8f),
			Vector3(0.0f, 0.85f, 6.0f),
			Vector3(2.1f, 0.6f, 5.8f),
			Vector3(4.2f, 0.3f, 5.5f),
		};
		constexpr std::array<Color, 5> colors = {
			Color(0.2f, 0.45f, 1.0f, 1.0f),
			Color(0.1f, 1.0f, 0.45f, 1.0f),
			Color(1.0f, 0.95f, 0.72f, 1.0f),
			Color(1.0f, 0.32f, 0.08f, 1.0f),
			Color(1.0f, 0.08f, 0.3f, 1.0f),
		};
		constexpr std::array<float, 5> intensityScales = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

		for (size_t index = 0; index < positions.size(); ++index)
		{
			components::TransformComponent transform{};
			transform.m_Position = positions[index];
			transform.m_Scale = Vector3::One * (0.5f + static_cast<float>(index) * 0.1f);
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(std::format(
				"gglab.lab.post_process.material.emitter.{}",
				index));
			material.m_Properties.m_BaseColor = Color(
				colors[index].m_R * 0.08f,
				colors[index].m_G * 0.08f,
				colors[index].m_B * 0.08f,
				1.0f);
			material.m_Properties.m_RoughnessFactor = 0.25f;
			const entt::entity emitter = primitive::Sphere::Create({
				.m_AssetManager = m_Services.m_AssetManager,
				.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
				.m_World = &m_World,
				.m_Transform = transform,
				.m_MaterialInstance = material,
			});
			registry.emplace<BloomEmitterComponent>(emitter, BloomEmitterComponent{
				.m_Color = colors[index],
				.m_IntensityScale = intensityScales[index],
			});
		}

		const entt::entity lightEntity = registry.create();
		components::TransformComponent lightTransform{};
		Vector3 direction(-0.35f, -0.8f, 0.45f);
		direction.Normalize();
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color(0.45f, 0.52f, 0.68f, 1.0f);
		light.m_Intensity = 0.6f;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(lightEntity, light);

		ApplyImmediateParameters();
	}

	void PostProcessLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(
			Vector3(0.0f, 2.1f, -7.5f),
			Vector3(0.0f, 0.35f, 5.8f));
		GetCamera().SetFov(46.0f);
		GetCamera().Update();
	}

	LabId PostProcessLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.post_process");
	}

	LabDescriptor PostProcessLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Post Process",
			.m_Category = "Rendering",
			.m_Description = "Validates exposure-aware Bloom over controlled HDR emitters.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> PostProcessLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<PostProcessLabSession>(createInfo);
	}
}
