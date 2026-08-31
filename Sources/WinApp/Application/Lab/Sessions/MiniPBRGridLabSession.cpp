#include "Application/Lab/Sessions/MiniPBRGridLabSession.h"
#include "AppRuntimeLog.h"
#include "GGLabRuntime/Core/Math/BoundingVolumes.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "Graphics/Asset/Loading/AssetLoadProgress.h"
#include "Graphics/Asset/AssetManager.h"
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
		constexpr float TargetAssetModelExtent = 18.0f;
		constexpr std::string_view MetalRoughSpheresPath =
			"Assets/Models/MetalRoughSpheres/MetalRoughSpheres.gltf";
		constexpr std::string_view MetalRoughSpheresNoTexturesPath =
			"Assets/Models/MetalRoughSpheresNoTextures/MetalRoughSpheresNoTextures.gltf";

		enum class SceneSource : int32_t
		{
			ProceduralGrid,
			MetalRoughSpheres,
			MetalRoughSpheresNoTextures,
		};

		const LabParameterId SceneSourceId("mini_pbr.scene.source");
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

		bool ComputeModelBounds(
			const Model& model, const AssetManager& assetManager, math::Aabb& result) noexcept
		{
			bool hasBounds = false;
			for (const ModelMesh& modelMesh : model.m_MeshInstance)
			{
				const Mesh* mesh = assetManager.GetMesh(modelMesh.m_MeshId);
				if (!mesh || !mesh->m_HasBounds)
				{
					continue;
				}

				const math::Aabb transformedBounds =
					math::Transform(mesh->m_Aabb, modelMesh.m_LocalTransform);
				if (!hasBounds)
				{
					result = transformedBounds;
					hasBounds = true;
				}
				else
				{
					result = math::Merge(result, transformedBounds);
				}
			}
			return hasBounds;
		}
	}

	MiniPBRGridLabSession::MiniPBRGridLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = SceneSourceId,
			.m_Name = "Scene Source",
			.m_Group = "Scene",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RebuildScene,
			.m_DefaultValue = int32_t(SceneSource::ProceduralGrid),
			.m_EnumItems =
				{
					{.m_Value = int32_t(SceneSource::ProceduralGrid), .m_Name = "Procedural Grid"},
					{.m_Value = int32_t(SceneSource::MetalRoughSpheres),
						.m_Name = "MetalRoughSpheres"},
					{.m_Value = int32_t(SceneSource::MetalRoughSpheresNoTextures),
						.m_Name = "MetalRoughSpheresNoTextures"},
				},
			}));
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
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Color,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = Color(0.82f, 0.18f, 0.08f, 1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MetallicMinId,
			.m_Name = "Metallic Min",
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MetallicMaxId,
			.m_Name = "Metallic Max",
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 1.0f,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RoughnessMinId,
			.m_Name = "Roughness Min",
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.05f,
			.m_MinValue = LabValue(0.04f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = RoughnessMaxId,
			.m_Name = "Roughness Max",
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Float,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = 0.9f,
			.m_MinValue = LabValue(0.04f),
			.m_MaxValue = LabValue(1.0f),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DebugViewId,
			.m_Name = "Debug View",
			.m_Group = "Procedural Material",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(MaterialDebugView::Lit),
			.m_EnumItems =
				{
					{.m_Value = int32_t(MaterialDebugView::Lit), .m_Name = "Lit"},
					{.m_Value = int32_t(MaterialDebugView::BaseColor), .m_Name = "Base Color"},
					{.m_Value = int32_t(MaterialDebugView::Metallic), .m_Name = "Metallic"},
					{.m_Value = int32_t(MaterialDebugView::Roughness), .m_Name = "Roughness"},
					{.m_Value = int32_t(MaterialDebugView::Normal), .m_Name = "Normal"},
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

		ApplyImmediateParameters();
	}

	void MiniPBRGridLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_World.GetRegistry().clear();
		m_PrepareMode = PrepareMode::None;
		m_NextGridRow = 0;
		m_PendingModelId.Reset();
		m_PendingModelPath.clear();
		ApplyCameraPreset();

		const auto sceneSource = static_cast<SceneSource>(
			GetParameters().Get(SceneSourceId, int32_t(SceneSource::ProceduralGrid)));
		if (sceneSource == SceneSource::ProceduralGrid)
		{
			m_PrepareMode = PrepareMode::ProceduralGrid;
			m_LoadingProgress = {
				.m_Status = LoadingStatus::Preparing,
				.m_Fraction = 0.05f,
				.m_Stage = "Building procedural material grid",
				.m_Detail = std::format("Preparing row 1 of {}.", GridSize),
			};
			return;
		}

		const std::string_view modelPath = sceneSource == SceneSource::MetalRoughSpheres
			? MetalRoughSpheresPath
			: MetalRoughSpheresNoTexturesPath;
		m_PrepareMode = PrepareMode::AssetModel;
		m_LoadingProgress = {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.02f,
			.m_Stage = "Requesting model",
			.m_Detail = std::string(modelPath),
		};
		if (!BuildAssetModel(modelPath))
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Stage = "Model request failed";
			m_LoadingProgress.m_Detail = std::string(modelPath);
		}
	}

	void MiniPBRGridLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}

		if (m_PrepareMode == PrepareMode::ProceduralGrid)
		{
			if (m_NextGridRow < GridSize)
			{
				BuildProceduralGridRow(m_NextGridRow);
				++m_NextGridRow;
			}

			LoadingProgressBuilder progress;
			const float gridFraction =
				static_cast<float>(m_NextGridRow) / static_cast<float>(GridSize);
			progress.AddStep(0.85f,
				{
					.m_Status =
						m_NextGridRow == GridSize ? LoadingStatus::Ready : LoadingStatus::Preparing,
					.m_Fraction = gridFraction,
					.m_Stage = "Building procedural material grid",
					.m_Detail = m_NextGridRow < GridSize ? std::format("Preparing row {} of {}.",
															   m_NextGridRow + 1, GridSize)
														 : "Procedural grid complete.",
				});
			if (m_NextGridRow == GridSize)
			{
				const Mesh* sphereMesh = m_Services.m_AssetManager->GetMesh(ProceduralSphereMeshID);
				if (!sphereMesh)
				{
					progress.AddStep(0.15f, {
												.m_Status = LoadingStatus::Failed,
												.m_Fraction = 0.0f,
												.m_Stage = "Procedural mesh unavailable",
												.m_Detail = "ProceduralSphere",
						});
					m_LoadingProgress = progress.Build();
					return;
				}

				const AssetLoadProgress meshProgress = GetAssetLoadProgress(
					sphereMesh->m_State, AssetLoadKind::Mesh, sphereMesh->m_LoadProgress);
				progress.AddAssetStep(0.15f, meshProgress, "ProceduralSphere");
				m_LoadingProgress = progress.Build();
				if (!meshProgress.IsReady())
				{
					return;
				}
				BuildLighting();
				ApplyImmediateParameters();
				m_PrepareMode = PrepareMode::None;
				m_LoadingProgress = LoadingProgress::Ready();
			}
			else
			{
				progress.AddStep(0.15f, {
											.m_Status = LoadingStatus::Preparing,
											.m_Fraction = 0.0f,
											.m_Stage = "Waiting for procedural mesh upload",
											.m_Detail = "ProceduralSphere",
					});
				m_LoadingProgress = progress.Build();
			}
			return;
		}

		if (m_PrepareMode != PrepareMode::AssetModel || !m_PendingModelId.IsValid())
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Stage = "Model request unavailable";
			return;
		}

		auto& assetManager = *m_Services.m_AssetManager;
		const Model* model = assetManager.GetModel(m_PendingModelId);
		if (!model)
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Stage = "Model request lost";
			m_LoadingProgress.m_Detail = m_PendingModelPath;
			return;
		}

		const AssetLoadProgress modelProgress =
			GetAssetLoadProgress(model->m_State, AssetLoadKind::Model, model->m_LoadProgress);
		LoadingProgressBuilder progress;
		progress.AddCompletedStep(0.05f);
		progress.AddAssetStep(0.85f, modelProgress, m_PendingModelPath);
		progress.AddStep(0.10f, {
									.m_Status = LoadingStatus::Preparing,
									.m_Fraction = 0.0f,
									.m_Stage = "Creating scene instance",
									.m_Detail = m_PendingModelPath,
			});
		m_LoadingProgress = progress.Build();
		if (!modelProgress.IsReady())
		{
			return;
		}

		m_LoadingProgress.m_Fraction = 0.95f;
		m_LoadingProgress.m_Stage = "Creating scene instance";
		if (!FinalizeAssetModel())
		{
			m_LoadingProgress.m_Status = LoadingStatus::Failed;
			m_LoadingProgress.m_Detail = "The model has no usable bounds.";
			return;
		}
		BuildLighting();
		ApplyImmediateParameters();
		m_PrepareMode = PrepareMode::None;
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void MiniPBRGridLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(), "Mini PBR Grid must be ready before commit.");
	}

	void MiniPBRGridLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_PrepareMode = PrepareMode::None;
		m_PendingModelId.Reset();
		m_PendingModelPath.clear();
		m_World.GetRegistry().clear();
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

		const Color baseColor = parameters.Get(BaseColorId, Color(0.82f, 0.18f, 0.08f, 1.0f));
		const float metallicMin =
			std::min(parameters.Get(MetallicMinId, 0.0f), parameters.Get(MetallicMaxId, 1.0f));
		const float metallicMax =
			std::max(parameters.Get(MetallicMinId, 0.0f), parameters.Get(MetallicMaxId, 1.0f));
		const float roughnessMin =
			std::min(parameters.Get(RoughnessMinId, 0.05f), parameters.Get(RoughnessMaxId, 0.9f));
		const float roughnessMax =
			std::max(parameters.Get(RoughnessMinId, 0.05f), parameters.Get(RoughnessMaxId, 0.9f));
		const auto debugView = static_cast<MaterialDebugView>(
			parameters.Get(DebugViewId, int32_t(MaterialDebugView::Lit)));

		auto materialView =
			m_World.GetRegistry().view<GridCellComponent, components::MaterialInstanceComponent>();
		for (const entt::entity entity : materialView)
		{
			const auto& cell = materialView.get<GridCellComponent>(entity);
			auto& material = materialView.get<components::MaterialInstanceComponent>(entity);
			material.m_Properties.m_BaseColor = baseColor;
			material.m_Properties.m_MetallicFactor =
				std::lerp(metallicMin, metallicMax, GridFactor(cell.m_Column));
			material.m_Properties.m_RoughnessFactor =
				std::lerp(roughnessMin, roughnessMax, GridFactor(cell.m_Row));
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
		ApplyImmediateParameters();
		BeginPrepare();
	}

	void MiniPBRGridLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void MiniPBRGridLabSession::BuildProceduralGridRow(uint32_t row) noexcept
	{
		auto& registry = m_World.GetRegistry();
		const float halfGridSize = static_cast<float>(GridSize - 1) * 0.5f;
		for (uint32_t column = 0; column < GridSize; ++column)
		{
			components::TransformComponent transform{};
			transform.m_Position =
				Vector3((static_cast<float>(column) - halfGridSize) * GridSpacing,
					(halfGridSize - static_cast<float>(row)) * GridSpacing, GridDepth);
			transform.m_Scale = Vector3::One * 0.95f;

			components::MaterialInstanceComponent material{};
			const std::string key =
				std::format("gglab.lab.mini_pbr_grid.material.{}.{}", row, column);
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

	bool MiniPBRGridLabSession::BuildAssetModel(std::string_view path) noexcept
	{
		const auto request = GetAssetOwnerScope().LoadModelAsync(std::filesystem::path(path));
		if (!request.IsValid())
		{
			GGLAB_LOG_ERROR("Mini PBR Grid failed to request model '{}'.", path);
			return false;
		}

		m_PendingModelId = request.m_ModelId;
		m_PendingModelPath = path;
		return true;
	}

	bool MiniPBRGridLabSession::FinalizeAssetModel() noexcept
	{
		if (!m_PendingModelId.IsValid())
		{
			return false;
		}

		auto& assetManager = *m_Services.m_AssetManager;
		const Model* model = assetManager.GetModel(m_PendingModelId);
		if (!model)
		{
			GGLAB_LOG_ERROR("Mini PBR Grid lost pending model '{}'.", m_PendingModelPath);
			m_PendingModelId.Reset();
			m_PendingModelPath.clear();
			return false;
		}
		if (model->m_State == AssetState::Failed || model->m_State == AssetState::Cancelled)
		{
			GGLAB_LOG_ERROR("Mini PBR Grid failed to load model '{}'.", m_PendingModelPath);
			m_PendingModelId.Reset();
			m_PendingModelPath.clear();
			return false;
		}
		if (model->m_State != AssetState::Ready)
		{
			return false;
		}

		GGLAB_LOG_INFO("Mini PBR Grid loaded '{}' with {} mesh instances.", m_PendingModelPath,
			model->m_MeshInstance.size());

		math::Aabb bounds{};
		if (!ComputeModelBounds(*model, assetManager, bounds))
		{
			GGLAB_LOG_ERROR("Mini PBR Grid model '{}' has no valid bounds.", m_PendingModelPath);
			m_PendingModelId.Reset();
			m_PendingModelPath.clear();
			return false;
		}

		const Vector3 boundsExtents = bounds.m_Extents;
		const Vector3 boundsCenter = bounds.m_Center;
		const Vector3 fullExtent = boundsExtents * 2.0f;
		const float maxExtent = std::max({ fullExtent.m_X, fullExtent.m_Y, fullExtent.m_Z });
		if (maxExtent <= 0.0f)
		{
			GGLAB_LOG_ERROR("Mini PBR Grid model '{}' has degenerate bounds.", m_PendingModelPath);
			m_PendingModelId.Reset();
			m_PendingModelPath.clear();
			return false;
		}

		components::TransformComponent transform{};
		const float scale = TargetAssetModelExtent / maxExtent;
		transform.m_Scale = Vector3::One * scale;
		transform.m_Position = Vector3(0.0f, 0.0f, GridDepth) - boundsCenter * scale;

		auto& registry = m_World.GetRegistry();
		const entt::entity entity = registry.create();
		registry.emplace<components::TransformComponent>(entity, transform);
		registry.emplace<components::ModelComponent>(entity, components::ModelComponent{
																 .m_ModelId = m_PendingModelId,
			});
		m_PendingModelId.Reset();
		m_PendingModelPath.clear();
		return true;
	}

	void MiniPBRGridLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();
		components::TransformComponent lightTransform{};
		Vector3 lightDirection(-0.45f, -0.75f, 0.48f);
		lightDirection.Normalize();
		lightTransform.m_Rotation = math::RotationFromTo(Vector3::Forward, lightDirection);
		registry.emplace<components::TransformComponent>(lightEntity, lightTransform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void MiniPBRGridLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(0.0f, 1.8f, -20.0f), Vector3(0.0f, 0.0f, GridDepth));
		GetCamera().Update();
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
			.m_Description =
				"Compares runtime, textured and factor-only metallic-roughness sphere grids.",
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
