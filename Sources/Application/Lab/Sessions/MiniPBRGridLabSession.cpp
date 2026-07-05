#include "Core/Precompiled.h"
#include "Application/Lab/Sessions/MiniPBRGridLabSession.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Scene/Components.h"

namespace gglab
{
	namespace
	{
		constexpr uint32_t GridSize = 9;
		constexpr float GridSpacing = 2.4f;
		constexpr float GridDepth = 5.0f;

		const LabParameterId EnableCameraInputId("mini_pbr.camera.enable_input");
		const LabParameterId CameraFovId("mini_pbr.camera.fov");
		const LabParameterId BaseColorId("mini_pbr.material.base_color");
		const LabParameterId MetallicMinId("mini_pbr.material.metallic_min");
		const LabParameterId MetallicMaxId("mini_pbr.material.metallic_max");
		const LabParameterId RoughnessMinId("mini_pbr.material.roughness_min");
		const LabParameterId RoughnessMaxId("mini_pbr.material.roughness_max");
		const LabParameterId DebugViewId("mini_pbr.material.debug_view");
		const LabParameterId LightIntensityId("mini_pbr.lighting.intensity");

		struct GridCellComponent
		{
			uint32_t m_Row = 0;
			uint32_t m_Column = 0;
		};

		float GridFactor(uint32_t index) noexcept
		{
			return static_cast<float>(index) / static_cast<float>(GridSize - 1);
		}
	}

	MiniPBRGridLabSession::MiniPBRGridLabSession(
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
			.m_Id = CameraFovId,
			.m_Name = "Field of View",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 50.0f,
			.m_MinValue = LabValue(30.0f),
			.m_MaxValue = LabValue(90.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BaseColorId,
			.m_Name = "Base Color",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = Color(0.82f, 0.18f, 0.08f, 1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MetallicMinId,
			.m_Name = "Metallic Min",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MetallicMaxId,
			.m_Name = "Metallic Max",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RoughnessMinId,
			.m_Name = "Roughness Min",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.05f,
			.m_MinValue = LabValue(0.04f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RoughnessMaxId,
			.m_Name = "Roughness Max",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.9f,
			.m_MinValue = LabValue(0.04f),
			.m_MaxValue = LabValue(1.0f),
		}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DebugViewId,
			.m_Name = "Debug View",
			.m_Group = "Material Grid",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(MaterialDebugView::Lit),
			.m_EnumItems = {
				{ .m_Value = int32_t(MaterialDebugView::Lit), .m_Name = "Lit" },
				{ .m_Value = int32_t(MaterialDebugView::BaseColor), .m_Name = "Base Color" },
				{ .m_Value = int32_t(MaterialDebugView::Metallic), .m_Name = "Metallic" },
				{ .m_Value = int32_t(MaterialDebugView::Roughness), .m_Name = "Roughness" },
				{ .m_Value = int32_t(MaterialDebugView::Normal), .m_Name = "Normal" },
			},
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

		GetCamera().LookAt(Vector3(0.0f, 0.0f, -9.0f), Vector3(0.0f, 0.0f, GridDepth));
		GetCamera().SetPosition(Vector3(8.0f, -7.0f, -20.0f));
		ApplyImmediateParameters();
		RebuildScene();
	}

	void MiniPBRGridLabSession::Update(float deltaTime) noexcept
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

	void MiniPBRGridLabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		m_EnableCameraInput = parameters.Get(EnableCameraInputId, true);
		GetCamera().SetFov(parameters.Get(CameraFovId, 50.0f));

		const Color baseColor = parameters.Get(
			BaseColorId,
			Color(0.82f, 0.18f, 0.08f, 1.0f));
		const float metallicMin = std::min(
			parameters.Get(MetallicMinId, 0.0f),
			parameters.Get(MetallicMaxId, 1.0f));
		const float metallicMax = std::max(
			parameters.Get(MetallicMinId, 0.0f),
			parameters.Get(MetallicMaxId, 1.0f));
		const float roughnessMin = std::min(
			parameters.Get(RoughnessMinId, 0.05f),
			parameters.Get(RoughnessMaxId, 0.9f));
		const float roughnessMax = std::max(
			parameters.Get(RoughnessMinId, 0.05f),
			parameters.Get(RoughnessMaxId, 0.9f));
		const auto debugView = static_cast<MaterialDebugView>(
			parameters.Get(DebugViewId, int32_t(MaterialDebugView::Lit)));

		auto materialView = m_World.GetRegistry().view<
			GridCellComponent,
			components::MaterialInstanceComponent>();
		for (const entt::entity entity : materialView)
		{
			const auto& cell = materialView.get<GridCellComponent>(entity);
			auto& material = materialView.get<components::MaterialInstanceComponent>(entity);
			material.m_Properties.m_BaseColor = baseColor;
			material.m_Properties.m_MetallicFactor = std::lerp(
				metallicMin,
				metallicMax,
				GridFactor(cell.m_Column));
			material.m_Properties.m_RoughnessFactor = std::lerp(
				roughnessMin,
				roughnessMax,
				GridFactor(cell.m_Row));
			material.m_Properties.m_DebugView = debugView;
		}

		auto lightView = m_World.GetRegistry().view<components::LightComponent>();
		for (const entt::entity entity : lightView)
		{
			lightView.get<components::LightComponent>(entity).m_Intensity =
				parameters.Get(LightIntensityId, 3.0f);
		}
	}

	void MiniPBRGridLabSession::RebuildScene() noexcept
	{
		auto& registry = m_World.GetRegistry();
		registry.clear();

		for (uint32_t row = 0; row < GridSize; ++row)
		{
			for (uint32_t column = 0; column < GridSize; ++column)
			{
				components::TransformComponent transform{};
				transform.m_Position = Vector3(
					(static_cast<float>(column) - 1.0f) * GridSpacing,
					(1.0f - static_cast<float>(row)) * GridSpacing,
					GridDepth);
				transform.m_Scale = Vector3::One * 0.95f;

				components::MaterialInstanceComponent material{};
				const std::string key = std::format(
					"gglab.lab.mini_pbr_grid.material.{}.{}",
					row,
					column);
				material.m_Key = RuntimeMaterialKey(key);

				const entt::entity sphere = primitive::Sphere::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
					.m_World = &m_World,
					.m_Transform = transform,
					.m_MaterialInstance = material,
				});
				registry.emplace<GridCellComponent>(sphere, GridCellComponent{
					.m_Row = row,
					.m_Column = column,
				});
			}
		}

		const entt::entity lightEntity = registry.create();
		components::TransformComponent lightTransform{};
		Vector3 lightDirection(-0.45f, -0.75f, 0.48f);
		lightDirection.Normalize();
		Quaternion::FromToRotation(
			-Vector3::UnitZ,
			lightDirection,
			lightTransform.m_Rotation);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = color::White;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(lightEntity, light);
		ApplyImmediateParameters();
	}

	LabId MiniPBRGridLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.mini_pbr_grid");
	}

	LabDescriptor MiniPBRGridLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Mini PBR Grid",
			.m_Category = "Materials",
			.m_Description = "A procedural sphere grid for comparing metallic and roughness values.",
			.m_Kind = LabKind::Scene,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> MiniPBRGridLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<MiniPBRGridLabSession>(createInfo);
	}
}
