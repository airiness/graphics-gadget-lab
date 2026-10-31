#include "Application/Lab/Sessions/GIReferenceLabSession.h"
#include "Application/Content/RuntimeAssetBundle.h"
#include "Application/Demo/CoastalSceneReferenceViews.h"
#include "Application/Demo/CoastalSceneLightingProfiles.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Graphics/Asset/ModelTypes.h"
#include "GGLabRuntime/Core/Log/LogMacros.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Diagnostics/Snapshots/TransientResourcePoolSnapshot.h"
#include "GGLabRuntime/Graphics/RHI/RHIDevice.h"
#include "GGLabRuntime/Graphics/RenderScene.h"
#include "GGLabRuntime/Graphics/RenderServices.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Geometry.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Pipeline/GIProbeUpdate.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <tuple>
#include <map>
#include <vector>

namespace gglab
{
	namespace
	{
		const LabParameterId FixtureId("gi_reference.fixture");
		const LabParameterId ProbeEnabledId("gi_reference.probe_enabled");
		const LabParameterId ProbeRaysId("gi_reference.probe_rays");
		const LabParameterId ProbeDivisorId("gi_reference.probe_divisor");
		const LabParameterId ProbeUpdatesId("gi_reference.probe_updates");
		const LabParameterId ProbeRefreshId("gi_reference.probe_refresh_period");
		const LabParameterId ProbeSharedId("gi_reference.probe_shared_blend");
		const LabParameterId ProbeTraversalId("gi_reference.probe_traversal_only");
		const LabParameterId ProbeRotateId("gi_reference.probe_rotate");
		const LabParameterId ProbeDiagnosticsId("gi_reference.probe_allocation_diagnostics");
		const LabParameterId ProbeTransformId("gi_reference.probe_transform");

		const LabParameterId SamplesId("gi_reference.samples");
		const LabParameterId AccumulateId("gi_reference.accumulate");
		const LabParameterId BatchSamplesId("gi_reference.batch_samples");
		const LabParameterId DepthId("gi_reference.depth");
		const LabParameterId TilePixelsId("gi_reference.tile_pixels");
		const LabParameterId SeedId("gi_reference.seed");
		const LabParameterId AlbedoId("gi_reference.albedo");
		const LabParameterId ExposureId("gi_reference.ev100");
		const LabParameterId EnvironmentId("gi_reference.environment");
		const LabParameterId FilterId("gi_reference.filter");
		const LabParameterId IntensityId("gi_reference.intensity");
		const LabParameterId YawId("gi_reference.yaw");
		const LabParameterId SunId("gi_reference.sun_lux");
		const LabParameterId SecondaryAlbedoId("gi_reference.secondary_albedo");
		const LabParameterId AtmosphereId("gi_reference.atmosphere");
		const LabParameterId MaterialCaseId("gi_reference.material_case");
		constexpr uint32_t MaterialCaseCount = 23;
		const LabParameterId VisibilityMaterialId("gi_reference.visibility_material");
		const LabParameterId VisibilityGeometryId("gi_reference.visibility_geometry");
		constexpr uint32_t VisibilityMaterialCount = 12;

		RuntimeMaterialKey VisibilityMaterialKey(uint32_t value) noexcept
		{
			return RuntimeMaterialKey(std::format("gglab.lab.gi_reference.visibility.{}", value));
		}

		RuntimeMaterialKey MaterialCaseKey(uint32_t value) noexcept
		{
			return RuntimeMaterialKey(std::format("gglab.lab.gi_reference.material.{}", value));
		}

		class GIReferencePipeline final : public RenderPipelineBase
		{
		public:
			GIReferencePipeline(World& world, AssetManager& assets, GIReferenceCreateInfo info) noexcept :
				m_World(world), m_Assets(assets), m_Scene(info.m_RayScene),
				m_Raster(CreateRenderPipelineForwardPlus({ .m_GIReference = std::move(info) })) {}
			GIReferencePipeline(World& world, AssetManager& assets, GISpecularVisibilityCreateInfo info) noexcept :
				m_World(world), m_Assets(assets), m_Scene(info.m_RayScene),
				m_Raster(CreateRenderPipelineForwardPlus({ .m_GISpecularVisibility = std::move(info) })) {}
			GIReferencePipeline(World& world, AssetManager& assets, std::shared_ptr<GIProbeUpdateExperiment> update) noexcept :
				m_World(world), m_Assets(assets), m_Scene(update->GetRayScene()),
				m_Raster(CreateRenderPipelineForwardPlus({ .m_GIProbeUpdate = std::move(update) })) {}
			std::string_view GetName() const noexcept override { return "GI fixed-depth reference"; }
			ResolvedTemporalFramePlan ResolveTemporalFramePlan(TemporalFramePlanResolveInfo info) const noexcept override
			{
				return m_Raster->ResolveTemporalFramePlan(info);
			}
			MaterialDiagnosticPrewarmProgress PrewarmMaterialDiagnostics(const RenderServices& services,
				std::span<const uint64_t> variants) noexcept override
			{
				return m_Raster->PrewarmMaterialDiagnostics(services, variants);
			}
			RenderFrameValidationResult ValidateRenderFrame(const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				const auto status = m_Scene->Prepare(m_World, m_Assets,
					*services.m_Presentation->GetDevice(), *services.m_Samplers);
				if (status == RaySceneStatus::NotReady || status == RaySceneStatus::Unavailable)
				{
					return RenderFrameValidationResult::Skip("GI reference ray scene is not available");
				}
				if (status != RaySceneStatus::Prepared)
				{
					return RenderFrameValidationResult::ContractFailure("GI reference scene preparation failed",
						m_Scene->GetDiagnostics().m_Reason);
				}
				return m_Raster->ValidateRenderFrame(frame, services);
			}
			void BuildRenderGraph(RenderGraph& graph, const RenderFrameContext& frame,
				const RenderServices& services) noexcept override
			{
				m_Raster->BuildRenderGraph(graph, frame, services);
			}

		private:
			World& m_World;
			AssetManager& m_Assets;
			std::shared_ptr<RayScene> m_Scene;
			std::unique_ptr<RenderPipelineBase> m_Raster;
		};

		CameraReferenceView View(const char* id, const Vector3& position, const Vector3& target,
			float fov, float exposure) noexcept
		{
			return { .m_Id = id, .m_Name = id, .m_Purpose = "Frozen GI fixture receiver measurement",
				.m_Position = position, .m_Target = target, .m_VerticalFovDegrees = fov,
				.m_FarPlane = 100.0f, .m_ManualEV100 = exposure };
		}
	}

	GIReferenceLabSession::GIReferenceLabSession(const LabSessionCreateInfo& info) noexcept :
		LabSessionBase(GetDescriptor(), info, CreateRenderPipelineForwardPlus())
	{
		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({ .m_Id = FixtureId, .m_Name = "Fixture", .m_Group = "Reference",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = {
				{ 0, "F1 isolated sphere" }, { 1, "F1 isolated plane" }, { 2, "F2 sealed thick-wall room" },
				{ 3, "F3 sealed thin-wall room" }, { 4, "F4 sunlit coloured room" },
				{ 5, "Contract: sun transport" }, { 6, "Contract: off-camera sun blocker" },
				{ 7, "Contract: published environment sphere" },
				{ 8, "Contract: coupled diffuse planes" }, { 9, "Contract: reduced hit material and snapshots" },
				{ 10, "Contract: GGX environment visibility" }, { 11, "Coastal: GGX visibility diagnostics" },
				{ 12, "Coastal: diffuse reference" }, { 13, "Contract: small smooth receiver" },
				{ 14, "Contract: probe furnace" }, { 15, "Coastal: probe update cost" },
				{ 16, "Contract: probe slab" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = AccumulateId, .m_Name = "Accumulate bounded reference",
			.m_Group = "Reference", .m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = true }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SamplesId, .m_Name = "Samples per pixel", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(64), .m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(1048576)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = BatchSamplesId, .m_Name = "Samples per submission", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(64), .m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(4096)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SeedId, .m_Name = "Independent seed", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(61051) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = DepthId, .m_Name = "Maximum diffuse queries", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(1), .m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(256)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = TilePixelsId, .m_Name = "Maximum sampled pixels per tile (0 = auto)",
			.m_Group = "Reference", .m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(0), .m_MinValue = LabValue(uint32_t(0)), .m_MaxValue = LabValue(uint32_t(8388608)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = AlbedoId, .m_Name = "F1 / contract receiver albedo", .m_Group = "Reference",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "White" }, { 1, "Linear (0.2, 0.5, 0.8)" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = ExposureId, .m_Name = "EV100", .m_Group = "Reference",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(10), .m_EnumItems = { { 0, "0" }, { 10, "10" }, { 15, "15" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = EnvironmentId, .m_Name = "Contract environment", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "Direct constant" },
				{ 1, "Published constant cubemap" }, { 2, "Published directional cubemap" }, { 3, "Published Physical Sky" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = FilterId, .m_Name = "Environment filter", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "32-texel face" }, { 1, "Mip zero" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = IntensityId, .m_Name = "Contract environment intensity", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(1), .m_EnumItems = { { 1, "1" }, { 2, "2" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = YawId, .m_Name = "Contract environment yaw", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "0 degrees" }, { 90, "90 degrees" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SunId, .m_Name = "Contract sun illuminance", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(10000), .m_EnumItems = { { 10000, "10000 lux" }, { 20000, "20000 lux" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SecondaryAlbedoId, .m_Name = "Contract secondary albedo", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "Neutral (0.8,0.8,0.8)" }, { 1, "Red (0.8,0.05,0.05)" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = AtmosphereId, .m_Name = "Contract atmosphere", .m_Group = "Lighting contracts",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = false }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = MaterialCaseId, .m_Name = "Material contract case",
			.m_Group = "Material contracts", .m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate, .m_DefaultValue = uint32_t(0),
			.m_MinValue = LabValue(uint32_t(0)),
			.m_MaxValue = LabValue(uint32_t(MaterialCaseCount - 1)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = VisibilityMaterialId, .m_Name = "Visibility primary material",
			.m_Group = "Visibility contracts", .m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate, .m_DefaultValue = uint32_t(0),
			.m_MinValue = LabValue(uint32_t(0)), .m_MaxValue = LabValue(VisibilityMaterialCount - 1) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = VisibilityGeometryId, .m_Name = "Visibility geometry",
			.m_Group = "Visibility contracts", .m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::RestartSession, .m_DefaultValue = int32_t(0),
			.m_EnumItems = { { 0, "Open" }, { 1, "Closed" }, { 2, "Partial blocker" },
				{ 3, "Off-camera blocker" } } }));
		for (const auto& [id, name, value, maximum] : std::array<std::tuple<LabParameterId, const char*, uint32_t, uint32_t>, 4>{ {
			{ ProbeRaysId, "Probe rays", 256, 256 }, { ProbeDivisorId, "Probe update divisor", 1, 4 },
			{ ProbeUpdatesId, "Probe update limit (zero continuous)", 1, 1'000'000 },
			{ ProbeRefreshId, "Full refresh period (zero fixed schedule)", 0, 1'000'000 } } })
		{
			GGLAB_UNUSED(parameters.Add({ .m_Id = id, .m_Name = name, .m_Group = "Probe experiment",
				.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
				.m_DefaultValue = value, .m_MinValue = LabValue(uint32_t(0)), .m_MaxValue = LabValue(maximum) }));
		}
		for (const auto& [id, name, value] : std::array<std::tuple<LabParameterId, const char*, bool>, 5>{ {
			{ ProbeEnabledId, "Probe experiment enabled", true }, { ProbeSharedId, "Shared-memory blend", true },
			{ ProbeTraversalId, "Traversal-only control", false }, { ProbeRotateId, "Rotate probe directions", true },
			{ ProbeDiagnosticsId, "Detailed allocation diagnostics", true } } })
		{
			GGLAB_UNUSED(parameters.Add({ .m_Id = id, .m_Name = name, .m_Group = "Probe experiment",
				.m_Type = LabParameterType::Bool,
				.m_Impact = id == ProbeEnabledId ? LabChangeImpact::RecreatePipeline : LabChangeImpact::RestartSession,
				.m_DefaultValue = value }));
		}
		GGLAB_UNUSED(parameters.Add({ .m_Id = ProbeTransformId, .m_Name = "Probe scene translation step",
			.m_Group = "Probe experiment", .m_Type = LabParameterType::UInt,
			.m_Impact = LabChangeImpact::Immediate, .m_DefaultValue = uint32_t(0),
			.m_MinValue = LabValue(uint32_t(0)), .m_MaxValue = LabValue(uint32_t(2)) }));
		auto& profile = GetMutableViewRenderProfile();
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		profile.m_EnableScenePreExposure = true;
	}

	void GIReferenceLabSession::BeginPrepare() noexcept
	{
		m_World.GetRegistry().clear();
		m_World.m_Atmosphere.reset();
		m_Assets.Reset();
		m_ProbeUpdate.reset();
		m_FirstProbeFrame = 0;
		m_LastProbeFrame = 0;
		m_ActivationGpuFrame = 0;
		m_InitializationGpuFrame = 0;
		m_InitializationGpuMilliseconds = 0;
		m_ActivationGpu.clear();
		m_ProbeEntity = entt::null;
		const auto fixture = GetParameters().Get(FixtureId, int32_t(0));
		const bool coastal = fixture == 11 || fixture == 12 || fixture == 15;
		const float exposure = static_cast<float>(GetParameters().Get(ExposureId, int32_t(10)));
		Color albedo = Color::White;
		if (fixture == 2 || fixture == 3)
		{
			albedo = Color(0.5f, 0.5f, 0.5f, 1.0f);
		}
		else if (fixture == 4)
		{
			albedo = Color(0.8f, 0.8f, 0.8f, 1.0f);
		}
		else if (GetParameters().Get(AlbedoId, int32_t(0)) == 1)
		{
			albedo = Color(0.2f, 0.5f, 0.8f, 1.0f);
		}
		components::MaterialInstanceComponent material;
		material.m_Key = RuntimeMaterialKey("gglab.lab.gi_reference.diffuse");
		material.m_Properties.m_BaseColor = albedo;
		material.m_Properties.m_MetallicFactor = 0.0f;
		material.m_Properties.m_RoughnessFactor = 1.0f;
		// The production fallback normal is RGBA8; zero scale makes the fixture's
		// no-normal-map contract exact instead of inheriting its quantized XY tilt.
		material.m_Properties.m_NormalScale = 0.0f;
		primitive::PrimitiveBase::CreateInfo primitiveInfo{ .m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_RenderServices.m_Samplers, .m_World = &m_World,
			.m_MaterialInstance = material };
		std::vector<CameraReferenceView> views;
		if (coastal)
		{
			std::string failure;
			const auto bundle = LoadRuntimeAssetBundle(
				m_Services.m_AssetRoot / "Models/GGLabCoastalRetreat/bundle.json", failure);
			if (!bundle)
			{
				m_Progress = { .m_Status = LoadingStatus::Failed, .m_Stage = "Coastal bundle unavailable",
					.m_Detail = std::move(failure) };
				return;
			}
			m_CaptureBundle = FrameCaptureAssetBundle{ bundle->m_Id, bundle->m_Revision };
			const auto model = GetAssetOwnerScope().LoadModelAsync(bundle->m_EntryPoint).m_ModelId;
			const auto entity = m_World.GetRegistry().create();
			m_World.GetRegistry().emplace<components::TransformComponent>(entity);
			m_World.GetRegistry().emplace<components::ModelComponent>(entity, model);
			m_Assets.TrackModel(model, "Coastal GI geometry");
			m_ProbeEntity = entity;
			views.assign(CoastalSceneReferenceViews.begin(), CoastalSceneReferenceViews.end());
		}
		else if (fixture == 14 || fixture == 16)
		{
			m_ProbeEntity = fixture == 16 ? primitive::Cube::Create(primitiveInfo) : primitive::Sphere::Create(primitiveInfo);
			m_Assets.TrackModel(fixture == 16 ? ProceduralCubeModelID : ProceduralSphereModelID, "Probe query contract");
			views.push_back(View("Probe_Contract", { 0, 0, -4 }, { 0, 0, 0 }, 45, exposure));
		}
		else if (fixture == 13)
		{
			// A two-centimetre plane with deliberately tilted smooth normals exposes
			// absolute derivative-area floors without changing frozen F1-F10 inputs.
			auto& assets = *m_Services.m_AssetManager;
			auto mesh = std::make_unique<Mesh>();
			mesh->m_Name = StringID("Small smooth GI receiver");
			AssetManager::MeshUploadData upload;
			Vector3 normal(0.3f, 0.4f, -1.0f);
			normal.Normalize();
			for (const auto uv : std::array<Vector2, 4>{ Vector2(0, 1), Vector2(0, 0),
				Vector2(1, 0), Vector2(1, 1) })
			{
				upload.m_VerticesData.push_back({ Vector3(uv.m_X * 2 - 1, 1 - uv.m_Y * 2, 0),
					normal, uv, uv, Vector4(1, 0, 0, 1) });
			}
			upload.m_IndicesData = { 0, 1, 2, 0, 2, 3 };
			const auto meshId = assets.AddProceduralMesh(std::move(mesh), upload);
			auto model = std::make_unique<Model>();
			model->m_Name = StringID("Small smooth GI receiver");
			model->m_MeshInstance.push_back({ meshId, ProceduralPrimitiveMaterialID });
			const auto modelId = assets.AddProceduralModel(std::move(model));
			const auto* stored = assets.GetModel(modelId);
			const bool retained = stored && GetAssetOwnerScope().RetainModel(
				modelId, stored->m_ContentGeneration);
			GGLAB_ASSERT(retained);
			auto& registry = m_World.GetRegistry();
			const auto entity = registry.create();
			components::TransformComponent transform;
			transform.m_Scale = Vector3(0.01f, 0.01f, 0.01f);
			registry.emplace<components::TransformComponent>(entity, transform);
			registry.emplace<components::ModelComponent>(entity, modelId);
			registry.emplace<components::MaterialInstanceComponent>(entity, material);
			m_Assets.TrackModel(modelId, "Small smooth GI receiver");
			views = {
				View("Contract_SmallReceiver", { 0, 0, -0.04f }, { 0, 0, 0 }, 35, exposure),
				View("Contract_SmallReceiverGrazing", { -0.038f, 0, -0.012f }, { 0, 0, 0 }, 35, exposure),
			};
			for (auto& view : views)
			{
				view.m_NearPlane = 0.0001f;
				view.m_FarPlane = 1.0f;
			}
		}
		else if (fixture == 10)
		{
			BuildVisibilityContract();
			views = {
				View("Contract_VisibilityNormal", { 0, 0, -4 }, { 0, 0, 0 }, 35, exposure),
				View("Contract_VisibilityGrazing", { -3.8f, 0, -1.2f }, { 0, 0, 0 }, 35, exposure),
			};
		}
		else if (fixture == 9)
		{
			BuildMaterialContract();
			views = {
				View("Contract_MaterialWall", { 0, 2, -3 }, { 0, 2, 2 }, 35, exposure),
				View("Contract_MaterialFloor", { 0, 1.5f, -3 }, { 0, 0, 0 }, 45, exposure),
			};
		}
		else if (fixture == 0 || fixture == 7)
		{
			GGLAB_UNUSED(primitive::Sphere::Create(primitiveInfo));
			m_Assets.TrackModel(ProceduralSphereModelID, "F1 sphere", 1.0f);
			views.push_back(View(fixture == 7 ? "Contract_Environment" : "F1_Sphere", { 0, 0, -4 }, { 0, 0, 0 }, 45.0f, exposure));
		}
		else if (fixture == 1)
		{
			primitiveInfo.m_Transform.m_Scale = Vector3(2.0f, 2.0f, 1.0f);
			primitiveInfo.m_Transform.m_Rotation = math::RotationFromTo(Vector3(0, 0, -1), Vector3::Up);
			GGLAB_UNUSED(primitive::Plane::Create(primitiveInfo));
			m_Assets.TrackModel(ProceduralPlaneModelID, "F1 plane", 1.0f);
			views.push_back(View("F1_Plane", { 0, 3, -3 }, { 0, 0, 0 }, 45.0f, exposure));
		}
		else if (fixture == 5 || fixture == 6 || fixture == 8)
		{
			// Receiver floor and uniformly sunlit wall. The CPU oracle integrates
			// the wall's projected solid angle independently of shader sampling.
			primitiveInfo.m_Transform.m_Scale = Vector3(2, 2, 1);
			primitiveInfo.m_Transform.m_Rotation = math::RotationFromTo(Vector3(0, 0, -1), Vector3::Up);
			GGLAB_UNUSED(primitive::Plane::Create(primitiveInfo));
			primitiveInfo.m_Transform.m_Position = Vector3(0, 2, 2);
			primitiveInfo.m_Transform.m_Rotation = Quaternion::Identity;
			primitiveInfo.m_MaterialInstance->m_Key = RuntimeMaterialKey("gglab.lab.gi_reference.secondary");
			primitiveInfo.m_MaterialInstance->m_Properties.m_BaseColor =
				GetParameters().Get(SecondaryAlbedoId, int32_t(0)) == 1 ?
					Color(0.8f, 0.05f, 0.05f, 1.0f) :
					Color(0.8f, 0.8f, 0.8f, 1.0f);
			GGLAB_UNUSED(primitive::Plane::Create(primitiveInfo));
			m_Assets.TrackModel(ProceduralPlaneModelID, "Sun transport planes", 1.0f);
			if (fixture == 6)
			{
				// Above the entire locked view. It blocks sunlight at the wall,
				// while receiver-to-wall paths remain identical to the unblocked arm.
				primitiveInfo.m_Transform.m_Position = Vector3(0, 4.6f, -4);
				primitiveInfo.m_Transform.m_Scale = Vector3(10, 0.1f, 8);
				GGLAB_UNUSED(primitive::Cube::Create(primitiveInfo));
				m_Assets.TrackModel(ProceduralCubeModelID, "Off-camera sun blocker", 1.0f);
			}
			views.push_back(View("Contract_Sun", { 0, 1.5f, -3 }, { 0, 0, 0 }, 45, exposure));
		}
		else
		{
			// Frozen specification v1; disjoint solid interiors, shared boundaries.
			const float t = fixture == 2 ? 1.0f : 0.2f;
			std::vector<std::pair<Vector3, Vector3>> solids
			{
				{ Vector3(-2 - t, -t, -2 - t), Vector3(2 + t, 0, 2 + t) },
				{ Vector3(-2 - t, 4, -2 - t), Vector3(2 + t, 4 + t, 2 + t) },
				{ Vector3(-2 - t, 0, -2 - t), Vector3(-2, 4, 2 + t) },
				{ Vector3(2, 0, -2 - t), Vector3(2 + t, 4, 2 + t) },
				{ Vector3(-2, 0, -2 - t), Vector3(2, 4, -2) },
			};
			if (fixture == 4)
			{
				solids.emplace_back(Vector3(-2, 0, 2), Vector3(-1, 4, 2 + t));
				solids.emplace_back(Vector3(1, 0, 2), Vector3(2, 4, 2 + t));
				solids.emplace_back(Vector3(-1, 3, 2), Vector3(1, 4, 2 + t));
			}
			else
			{
				solids.emplace_back(Vector3(-2, 0, 2), Vector3(2, 4, 2 + t));
			}
			for (size_t index = 0; index < solids.size(); ++index)
			{
				const auto& [minimum, maximum] = solids[index];
				primitiveInfo.m_Transform.m_Position = (minimum + maximum) * 0.5f;
				primitiveInfo.m_Transform.m_Scale = (maximum - minimum) * 0.5f;
				primitiveInfo.m_MaterialInstance = material;
				if (fixture == 4 && (index == 2 || index == 3))
				{
					primitiveInfo.m_MaterialInstance->m_Key = RuntimeMaterialKey(index == 2 ?
						"gglab.lab.gi_reference.west" : "gglab.lab.gi_reference.east");
					primitiveInfo.m_MaterialInstance->m_Properties.m_BaseColor = index == 2 ?
						Color(0.8f, 0.05f, 0.05f, 1.0f) : Color(0.05f, 0.8f, 0.05f, 1.0f);
				}
				GGLAB_UNUSED(primitive::Cube::Create(primitiveInfo));
			}
			m_Assets.TrackModel(ProceduralCubeModelID, "Frozen room shell", 1.0f);
			const auto prefix = fixture == 2 ? "F2" : "F3";
			if (fixture == 4)
			{
				// With the frozen opening/direction, sunlight reaches the floor before
				// either coloured wall. The neutral floor/jamb still reflect sun at D=1.
				views = {
					View("F4_Overview", { 0, 1.6f, 5 }, { 0, 1.6f, 0 }, 50, exposure),
					View("F4_Floor", { 0, 1.6f, 1.4f }, { 0, 0, -0.5f }, 90, exposure),
					View("F4_Back", { 0, 1.6f, 1.4f }, { 0, 1.6f, -1 }, 60, exposure),
					View("F4_West", { 0, 1.6f, 0 }, { -2, 2, 0 }, 90, exposure),
					View("F4_East", { 0, 1.6f, 0 }, { 2, 2, 0 }, 90, exposure),
				};
			}
			else
			{
				views = {
					View((std::string(prefix) + "_Interior").c_str(), { 0, 1.6f, 1.4f }, { 0, 1.6f, -1 }, 60, exposure),
					View((std::string(prefix) + "_Floor").c_str(), { 0, 1.6f, 1.4f }, { 0, 0, 0 }, 90, exposure),
					View((std::string(prefix) + "_Ceiling").c_str(), { 0, 1.6f, 1.4f }, { 0, 4, 0 }, 90, exposure),
					View((std::string(prefix) + "_West").c_str(), { 0, 1.6f, 0 }, { -2, 2, 0 }, 90, exposure),
					View((std::string(prefix) + "_East").c_str(), { 0, 1.6f, 0 }, { 2, 2, 0 }, 90, exposure),
					View((std::string(prefix) + "_Front").c_str(), { 0, 1.6f, -1.4f }, { 0, 1.6f, 2 }, 60, exposure),
					View((std::string(prefix) + "_Roof").c_str(), { 0, 8, -3 }, { 0, 5, 0 }, 60, exposure),
				};
			}
		}
		const bool contract = fixture >= 5;
		const auto environment = coastal ?
			3 : contract ? GetParameters().Get(EnvironmentId, int32_t(0)) : 0;
		if (fixture == 4 || fixture == 5 || fixture == 6 || fixture == 8 || fixture == 9 || environment == 3)
		{
			auto& registry = m_World.GetRegistry();
			const auto light = registry.create();
			Vector3 direction = coastal ?
				CoastalSceneLightingProfiles.front().m_Inputs.m_SunDirection :
				fixture == 4 ? Vector3(0.2f, -1, -1) : Vector3(0, -1, 1);
			direction.Normalize();
			components::TransformComponent transform;
			transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			registry.emplace<components::TransformComponent>(light, transform);
			components::LightComponent sun;
			sun.m_WorldSun = WorldSunSettings{
				.m_PerpendicularIlluminanceLux = fixture == 4 ? 10000.0f :
					static_cast<float>(GetParameters().Get(SunId, int32_t(10000))) };
			if (coastal)
			{
				sun.m_WorldSun = CoastalSceneLightingProfiles.front().m_Inputs.m_Sun;
				if (fixture == 12 || fixture == 15)
				{
					sun.m_DirectionalShadowSettings.emplace();
				}
			}
			registry.emplace<components::LightComponent>(light, sun);
			if (environment == 3 || (contract && GetParameters().Get(AtmosphereId, false)))
			{
				m_World.m_Atmosphere = AtmosphereSettings{};
			}
		}
		const auto firstView = views.front().m_Id;
		const bool registered = GetCameraRig().SetReferenceViews(std::move(views));
		const bool restored = GetCameraRig().RestoreReferenceView(firstView);
		GGLAB_ASSERT(registered && restored);
		if (fixture >= 12)
		{
			const auto& camera = GetCamera();
			const CameraPathKey key{ .m_Frame = 0, .m_Position = camera.GetPosition(),
				.m_Target = camera.GetPosition() + camera.GetForward(), .m_VerticalFovDegrees = camera.GetFov() };
			auto last = key;
			last.m_Frame = 359;
			const bool paths = GetCameraRig().SetCameraPaths({ CameraPath{
				.m_Id = "GI_CostStatic", .m_Name = "Probe cost static view",
				.m_Purpose = "Matched-input GI reference and probe update cost measurement",
				.m_NearPlane = camera.GetNear(), .m_FarPlane = camera.GetFar(),
				.m_ManualEV100 = camera.GetManualEV100(), .m_Keys = { key, last } } });
			GGLAB_ASSERT(paths);
		}
		m_RayScene = std::make_shared<RayScene>(RaySceneCapacity{},
			fixture >= 10 && fixture <= 13 ? RaySceneMaterialInputs::PrimaryReceiver : RaySceneMaterialInputs::ReducedDiffuse,
			fixture >= 14 && GetParameters().Get(ProbeDiagnosticsId, true));
		GIReferenceCreateInfo reference{ .m_RayScene = m_RayScene,
			.m_SampleCount = GetParameters().Get(SamplesId, uint32_t(64)),
			.m_Seed = GetParameters().Get(SeedId, uint32_t(61051)) };
		reference.m_SamplesPerBatch = GetParameters().Get(BatchSamplesId, uint32_t(64));
		reference.m_Accumulate = GetParameters().Get(AccumulateId, true);
		reference.m_MaximumDepth = GetParameters().Get(DepthId, uint32_t(1));
		reference.m_MaximumDistance = coastal ? 30000.0f : 1000.0f;
		reference.m_HitMaterialDiagnostics = fixture == 9;
		reference.m_MaximumTilePixels = GetParameters().Get(TilePixelsId, uint32_t(0));
		reference.m_ConstantEnvironment = environment == 0;
		// The HDR importer decodes RGBE bin centres: (128 + 0.5) / 128.
		// Match that exact constant in the independent direct-input contract.
		const float radiance = fixture == 4 ? 0.1f :
			fixture == 5 || fixture == 6 ? 0.0f :
				fixture == 7 ? 1.00390625f : 1.0f;
		reference.m_ConstantRadiance = { radiance, radiance, radiance };
		reference.m_EnvironmentFilter = contract && GetParameters().Get(FilterId, int32_t(0)) == 1 ?
			GIReferenceEnvironmentFilter::MipZero : GIReferenceEnvironmentFilter::Face32;
		if (fixture >= 14)
		{
			RecreatePipeline();
		}
		else if (fixture == 10 || fixture == 11)
		{
			GISpecularVisibilityCreateInfo visibility{ .m_RayScene = m_RayScene,
				.m_SampleCount = reference.m_SampleCount, .m_Seed = reference.m_Seed,
				.m_SamplesPerBatch = reference.m_SamplesPerBatch,
				.m_MaximumTilePixels = reference.m_MaximumTilePixels,
				.m_MaximumDistance = fixture == 11 ? 30000.0f : 1000.0f };
			SetRenderPipeline(std::make_unique<GIReferencePipeline>(m_World,
				*m_Services.m_AssetManager, std::move(visibility)));
		}
		else
		{
			SetRenderPipeline(std::make_unique<GIReferencePipeline>(m_World,
				*m_Services.m_AssetManager, std::move(reference)));
		}
		m_World.MarkLightingAuthoringChanged();
		m_Progress = m_Assets.BuildProgress(*m_Services.m_AssetManager, "Preparing GI reference fixture");
		UpdateMaterialProgress();
	}

	void GIReferenceLabSession::BuildVisibilityContract() noexcept
	{
		auto& assets = *m_Services.m_AssetManager;
		primitive::PrimitiveBase::CreateInfo info{ .m_AssetManager = &assets,
			.m_SamplerRegistry = m_Services.m_RenderServices.m_Samplers, .m_World = &m_World };
		info.m_Transform.m_Scale = Vector3(2, 2, 1);
		m_VisibilityReceiver = primitive::Plane::Create(info);
		m_Assets.TrackModel(ProceduralPlaneModelID, "Visibility receiver");
		// A curved interpolated normal field on planar geometry gives the CPU
		// oracle an authored, nonzero specular-AA derivative contract.
		auto mesh = std::make_unique<Mesh>();
		mesh->m_Name = StringID("Visibility normal field");
		AssetManager::MeshUploadData upload;
		for (const auto uv : std::array<Vector2, 4>{ Vector2(0, 1), Vector2(0, 0),
			Vector2(1, 0), Vector2(1, 1) })
		{
			const float x = uv.m_X * 2 - 1;
			const float y = 1 - uv.m_Y * 2;
			upload.m_VerticesData.push_back({ Vector3(x, y, 0), Vector3(x * 2, y * 2, -1),
				uv, uv, Vector4(1, 0, 0, 1) });
		}
		upload.m_IndicesData = { 0, 1, 2, 0, 2, 3 };
		const auto meshId = assets.AddProceduralMesh(std::move(mesh), upload);
		auto model = std::make_unique<Model>();
		model->m_Name = StringID("Visibility normal field");
		model->m_MeshInstance.push_back({ meshId, ProceduralPrimitiveMaterialID });
		m_VisibilityNormalModel = assets.AddProceduralModel(std::move(model));
		const auto* storedModel = assets.GetModel(m_VisibilityNormalModel);
		const bool retained = storedModel && GetAssetOwnerScope().RetainModel(
			m_VisibilityNormalModel, storedModel->m_ContentGeneration);
		GGLAB_ASSERT(retained);
		m_Assets.TrackModel(m_VisibilityNormalModel, "Visibility normal-field geometry");
		wchar_t* root = nullptr;
		size_t length = 0;
		GGLAB_UNUSED(_wdupenv_s(&root, &length, L"GGLAB_GI_VISIBILITY_FIXTURE_ROOT"));
		const std::filesystem::path directory = root ? root : L"";
		std::free(root);
		constexpr std::array files{ "normal.dds", "coat-normal.dds", "anisotropy.dds" };
		if (directory.is_absolute())
		{
			for (size_t index = 0; index < files.size(); ++index)
			{
				m_VisibilityTextures[index] = GetAssetOwnerScope().LoadTextureAsync(directory / files[index],
					index < 2 ? TextureSemantic::Normal : TextureSemantic::Anisotropy).m_TextureId;
			}
		}
		ApplyVisibilityMaterial();
		const auto geometry = GetParameters().Get(VisibilityGeometryId, int32_t(0));
		if (geometry != 0)
		{
			// The closed cube is viewed from inside with back-face culling: only
			// the receiver rasterizes, but all six opaque faces occlude ray misses.
			info.m_Transform.m_Position = geometry == 1 ?
				Vector3(0, 0, 0) :
				geometry == 2 ? Vector3(0.8f, 0, -1.1f) : Vector3(3, 0, -1);
			info.m_Transform.m_Scale = geometry == 1 ?
				Vector3(6, 6, 6) :
				geometry == 2 ? Vector3(0.7f, 1.5f, 0.1f) : Vector3(0.3f, 4, 0.5f);
			GGLAB_UNUSED(primitive::Cube::Create(info));
			m_Assets.TrackModel(ProceduralCubeModelID, "Visibility blocker");
		}
	}

	void GIReferenceLabSession::ApplyVisibilityMaterial() noexcept
	{
		auto& registry = m_World.GetRegistry();
		if (!registry.valid(m_VisibilityReceiver))
		{
			return;
		}
		const auto value = GetParameters().Get(VisibilityMaterialId, uint32_t(0));
		components::MaterialInstanceComponent instance;
		instance.m_Key = VisibilityMaterialKey(value);
		auto& material = instance.m_Properties;
		material.m_BaseColor = Color(0.8f, 0.2f, 0.05f, 1);
		material.m_MetallicFactor = value == 3 ? 1.0f : 0.0f;
		material.m_RoughnessFactor = value == 1 ?
			1.0f :
			value == 2 || value == 8 || value == 11 ? 0.045f : value >= 4 ? 0.2f : 0.5f;
		material.m_NormalScale = value == 5 ? 1.0f : 0.0f;
		material.m_Ior = value == 8 ? 1.0f : value == 9 ? 0.0f : value == 10 ? 2.4f : 1.5f;
		material.m_ClearcoatFactor = value == 4 || value == 5 || value == 10 ? 0.8f : 0.0f;
		material.m_ClearcoatRoughness = 0.75f;
		material.m_AnisotropyStrength = value == 6 || value == 7 ? 0.8f : 0.0f;
		material.m_AnisotropyRotation = value == 7 ? 1.5707963267948966f : 0.0f;
		const auto sampler = m_Services.m_RenderServices.m_Samplers->GetPresetSamplerId(SamplerPreset::LinearClamp);
		if (value == 5)
		{
			material.m_NormalBinding.m_TextureId = m_VisibilityTextures[0];
			material.m_NormalBinding.m_SamplerId = sampler;
			material.m_ClearcoatNormalBinding.m_TextureId = m_VisibilityTextures[1];
			material.m_ClearcoatNormalBinding.m_SamplerId = sampler;
		}
		if (value == 6 || value == 7)
		{
			material.m_AnisotropyBinding.m_TextureId = m_VisibilityTextures[2];
			material.m_AnisotropyBinding.m_SamplerId = sampler;
		}
		if (value == 10)
		{
			material.m_ClearcoatBinding.m_TextureId = m_VisibilityTextures[2];
			material.m_ClearcoatBinding.m_SamplerId = sampler;
			material.m_ClearcoatRoughnessBinding = material.m_ClearcoatBinding;
			material.m_ClearcoatNormalBinding.m_TextureId = m_VisibilityTextures[1];
			material.m_ClearcoatNormalBinding.m_SamplerId = sampler;
		}
		registry.get<components::ModelComponent>(m_VisibilityReceiver).m_ModelId =
			value == 11 ? m_VisibilityNormalModel : ProceduralPlaneModelID;
		registry.emplace_or_replace<components::MaterialInstanceComponent>(m_VisibilityReceiver, instance);
	}

	void GIReferenceLabSession::RecreatePipeline() noexcept
	{
		const auto fixture = GetParameters().Get(FixtureId, int32_t(0));
		if (fixture < 14)
		{
			return;
		}
		m_FirstProbeFrame = 0;
		m_LastProbeFrame = 0;
		m_ActivationGpuFrame = 0;
		m_InitializationGpuFrame = 0;
		m_InitializationGpuMilliseconds = 0;
		m_ActivationGpu.clear();
		if (!GetParameters().Get(ProbeEnabledId, true))
		{
			m_ProbeUpdate.reset();
			m_RayScene.reset();
			SetRenderPipeline(CreateRenderPipelineForwardPlus());
			return;
		}
		if (m_ProbeEntity != entt::null)
		{
			auto& transform = m_World.GetRegistry().get<components::TransformComponent>(m_ProbeEntity);
			transform.m_Position.m_X = 0.1f * static_cast<float>(GetParameters().Get(ProbeTransformId, uint32_t(0)));
		}
		if (!m_RayScene)
		{
			m_RayScene = std::make_shared<RayScene>(RaySceneCapacity{},
				RaySceneMaterialInputs::ReducedDiffuse, GetParameters().Get(ProbeDiagnosticsId, true));
		}
		if (!m_ProbeUpdate)
		{
			GIProbeUpdateProfile profile;
			profile.m_RaysPerProbe = GetParameters().Get(ProbeRaysId, uint32_t(256));
			profile.m_UpdateDivisor = GetParameters().Get(ProbeDivisorId, uint32_t(1));
			profile.m_MaximumUpdates = GetParameters().Get(ProbeUpdatesId, uint32_t(1));
			profile.m_RefreshPeriod = GetParameters().Get(ProbeRefreshId, uint32_t(0));
			profile.m_SharedBlend = GetParameters().Get(ProbeSharedId, true);
			profile.m_TraversalOnly = GetParameters().Get(ProbeTraversalId, false);
			profile.m_RotateDirections = GetParameters().Get(ProbeRotateId, true);
			profile.m_Seed = GetParameters().Get(SeedId, uint32_t(61051));
			if (fixture != 15)
			{
				profile.m_Counts = { 2, 2, 2 };
				profile.m_Minimum = { -0.25f, -0.25f, -0.25f };
				profile.m_Spacing = 0.5f;
				profile.m_ConstantEnvironment = true;
				profile.m_ConstantRadiance = { 1, 0.5f, 2 };
				profile.m_Hysteresis = 0;
				profile.m_TraverseScene = fixture == 16;
			}
			m_ProbeUpdate = std::make_shared<GIProbeUpdateExperiment>(m_RayScene, profile);
		}
		SetRenderPipeline(std::make_unique<GIReferencePipeline>(m_World,
			*m_Services.m_AssetManager, m_ProbeUpdate));
	}

	void GIReferenceLabSession::BuildMaterialContract() noexcept
	{
		auto& assets = *m_Services.m_AssetManager;
		auto* samplers = m_Services.m_RenderServices.m_Samplers;
		wchar_t* fixtureRoot = nullptr;
		size_t fixtureRootLength = 0;
		const auto environmentResult = _wdupenv_s(
			&fixtureRoot, &fixtureRootLength, L"GGLAB_GI_MATERIAL_FIXTURE_ROOT");
		const std::filesystem::path directory = environmentResult == 0 && fixtureRoot ? fixtureRoot : L"";
		std::free(fixtureRoot);
		constexpr std::array files{ "pattern.dds", "metal.dds", "mip64.dds", "mip65.dds",
			"mip129.dds", "short256.dds", "replacement.dds", "normal.dds" };
		for (size_t index = 0; index < files.size(); ++index)
		{
			const auto semantic = index == 1 ? TextureSemantic::MetallicRoughness :
				index == 7 ? TextureSemantic::Normal : TextureSemantic::BaseColor;
			if (directory.is_absolute())
			{
				m_MaterialTextures[index] = GetAssetOwnerScope().LoadTextureAsync(
					directory / files[index], semantic).m_TextureId;
			}
		}
		components::MaterialInstanceComponent receiver;
		receiver.m_Key = RuntimeMaterialKey("gglab.lab.gi_reference.material.receiver");
		receiver.m_Properties.m_BaseColor = Color(0.7f, 0.7f, 0.7f, 1);
		receiver.m_Properties.m_MetallicFactor = 0;
		receiver.m_Properties.m_RoughnessFactor = 1;
		receiver.m_Properties.m_NormalScale = 0;
		primitive::PrimitiveBase::CreateInfo plane{ .m_AssetManager = &assets,
			.m_SamplerRegistry = samplers, .m_World = &m_World, .m_MaterialInstance = receiver };
		plane.m_Transform.m_Scale = Vector3(2, 2, 1);
		plane.m_Transform.m_Rotation = math::RotationFromTo(Vector3(0, 0, -1), Vector3::Up);
		GGLAB_UNUSED(primitive::Plane::Create(plane));
		m_Assets.TrackModel(ProceduralPlaneModelID, "Material contract receiver");
		// A separate procedural mesh makes UV1 observably different from UV0. Both
		// versions are prepared before updates; switching them replaces geometry.
		for (uint32_t version = 0; version < m_MaterialModels.size(); ++version)
		{
			auto mesh = std::make_unique<Mesh>();
			mesh->m_Name = StringID("GI material UV plane");
			AssetManager::MeshUploadData upload;
			for (const auto& uv : std::array<Vector2, 4>{ Vector2(0, 1), Vector2(0, 0),
				Vector2(1, 0), Vector2(1, 1) })
			{
				upload.m_VerticesData.push_back({ Vector3(uv.m_X * 2 - 1, 1 - uv.m_Y * 2, 0),
					Vector3(0, 0, -1), uv,
					Vector2(0.2f + 0.5f * uv.m_Y, 0.1f + (version == 0 ? 0.7f : 0.3f) * uv.m_X),
					Vector4(1, 0, 0, 1) });
			}
			upload.m_IndicesData = { 0, 1, 2, 0, 2, 3 };
			const auto meshId = assets.AddProceduralMesh(std::move(mesh), upload);
			auto model = std::make_unique<Model>();
			model->m_Name = StringID("GI material UV plane");
			model->m_MeshInstance.push_back({ meshId, ProceduralPrimitiveMaterialID });
			m_MaterialModels[version] = assets.AddProceduralModel(std::move(model));
			const auto* storedModel = assets.GetModel(m_MaterialModels[version]);
			const bool retained = storedModel && GetAssetOwnerScope().RetainModel(
				m_MaterialModels[version], storedModel->m_ContentGeneration);
			GGLAB_ASSERT(retained);
			m_Assets.TrackModel(m_MaterialModels[version], "Material contract UV geometry");
		}
		auto& registry = m_World.GetRegistry();
		m_MaterialWall = registry.create();
		registry.emplace<components::TransformComponent>(m_MaterialWall);
		registry.emplace<components::ModelComponent>(m_MaterialWall, m_MaterialModels[0]);
		registry.emplace<components::MaterialInstanceComponent>(m_MaterialWall);
		ApplyImmediateParameters();
	}

	void GIReferenceLabSession::UpdateMaterialProgress() noexcept
	{
		if (GetParameters().Get(FixtureId, int32_t(0)) == 10 && !m_Progress.HasFailed())
		{
			for (const auto texture : m_VisibilityTextures)
			{
				const auto state = m_Services.m_AssetManager->GetTextureState(
					m_Services.m_AssetManager->GetTextureContentRef(texture));
				if (!state || *state == AssetState::Failed)
				{
					m_Progress = { .m_Status = LoadingStatus::Failed, .m_Stage = "Visibility textures unavailable",
						.m_Detail = "Generate fixtures and set GGLAB_GI_VISIBILITY_FIXTURE_ROOT." };
					return;
				}
				if (*state != AssetState::Ready)
				{
					m_Progress = { .m_Status = LoadingStatus::Preparing, .m_Stage = "Loading visibility textures" };
				}
			}
			return;
		}
		if (GetParameters().Get(FixtureId, int32_t(0)) != 9 || m_Progress.HasFailed())
		{
			return;
		}
		for (const auto texture : m_MaterialTextures)
		{
			const auto content = m_Services.m_AssetManager->GetTextureContentRef(texture);
			const auto state = m_Services.m_AssetManager->GetTextureState(content);
			if (!state || *state == AssetState::Failed)
			{
				m_Progress = { .m_Status = LoadingStatus::Failed,
					.m_Stage = "Material fixtures unavailable",
					.m_Detail = "Generate textures with ValidateGIReferenceMaterials.py --generate and set GGLAB_GI_MATERIAL_FIXTURE_ROOT." };
				return;
			}
			if (*state != AssetState::Ready)
			{
				m_Progress = { .m_Status = LoadingStatus::Preparing, .m_Stage = "Loading material fixtures" };
				return;
			}
		}
	}

	void GIReferenceLabSession::ApplyImmediateParameters() noexcept
	{
		ApplyVisibilityMaterial();
		auto& registry = m_World.GetRegistry();
		if (registry.valid(m_ProbeEntity))
		{
			// The safe parameter batch mutates CPU authoring only. RayScene::Prepare
			// replaces immutable tables/TLAS and submission leases retain the old snapshot.
			auto& transform = registry.get<components::TransformComponent>(m_ProbeEntity);
			const float translation =
				0.1f * static_cast<float>(GetParameters().Get(ProbeTransformId, uint32_t(0)));
			if (transform.m_Position.m_X != translation)
			{
				transform.m_Position.m_X = translation;
				m_FirstProbeFrame = 0;
				m_LastProbeFrame = 0;
				m_ActivationGpuFrame = 0;
				m_ActivationGpu.clear();
			}
		}
		if (!registry.valid(m_MaterialWall))
		{
			return;
		}
		const auto value = GetParameters().Get(MaterialCaseId, uint32_t(0));
		if (value == 16 && m_MaterialCase != value)
		{
			const auto model = registry.get<components::ModelComponent>(m_MaterialWall);
			registry.destroy(m_MaterialWall);
			m_MaterialWall = registry.create();
			registry.emplace<components::TransformComponent>(m_MaterialWall);
			registry.emplace<components::ModelComponent>(m_MaterialWall, model);
			registry.emplace<components::MaterialInstanceComponent>(m_MaterialWall);
		}
		auto& transform = registry.get<components::TransformComponent>(m_MaterialWall);
		transform = {};
		transform.m_Position = Vector3(0, 2, value == 15 ? 3.0f : 2.0f);
		transform.m_Scale = Vector3(2, 2, 1);
		if (value == 13 || value == 14)
		{
			transform.m_Rotation = math::RotationFromTo(Vector3(0, 0, -1), Vector3(0, 0, 1));
		}
		registry.get<components::ModelComponent>(m_MaterialWall).m_ModelId =
			m_MaterialModels[value == 19 ? 1 : 0];
		auto& instance = registry.get<components::MaterialInstanceComponent>(m_MaterialWall);
		instance = {};
		instance.m_Key = MaterialCaseKey(value);
		auto& material = instance.m_Properties;
		material.m_BaseColor = value == 0 ? Color::White : Color(0.6f, 0.8f, 0.4f, 1);
		material.m_MetallicFactor = 0;
		material.m_NormalScale = 0;
		material.m_RoughnessFactor = 1;
		if ((value >= 2 && value <= 12) || value == 19 || value == 21)
		{
			const uint32_t texture = value >= 8 && value <= 11 ? value - 6 : value == 21 ? 6 : 0;
			material.m_BaseColorBinding.m_TextureId = m_MaterialTextures[texture];
			material.m_BaseColorBinding.m_SamplerId = m_Services.m_RenderServices.m_Samplers->
				GetPresetSamplerId(value == 6 ? SamplerPreset::PointClamp :
					value == 7 ? SamplerPreset::LinearWrap : SamplerPreset::LinearClamp);
		}
		if (value == 3)
		{
			material.m_MetallicFactor = 0.5f;
			material.m_MetallicRoughnessBinding.m_TextureId = m_MaterialTextures[1];
			material.m_MetallicRoughnessBinding.m_SamplerId = m_Services.m_RenderServices.m_Samplers->
				GetPresetSamplerId(SamplerPreset::PointClamp);
		}
		if (value == 4 || value == 19)
		{
			material.m_BaseColorBinding.m_TexCoordIndex = 1;
		}
		if (value == 5 || value == 6 || value == 7)
		{
			auto& binding = material.m_BaseColorBinding;
			binding.m_UVOffset = Vector2(-0.25f, 0.15f);
			binding.m_UVScale = Vector2(2.0f, 1.5f);
			binding.m_UVRotation = 0.5f;
		}
		if (value == 12)
		{
			// Secondary GI deliberately ignores these inputs. The floor receiver has
			// no normal map, so its primary direction distribution remains unchanged.
			material.m_NormalBinding.m_TextureId = m_MaterialTextures[7];
			material.m_NormalScale = 1;
			material.m_RoughnessFactor = 0.03f;
			material.m_EmissiveColor = Color(30, 10, 50, 1);
			material.m_EmissiveBinding.m_TextureId = m_MaterialTextures[0];
			material.m_ClearcoatFactor = 1;
			material.m_ClearcoatBinding.m_TextureId = m_MaterialTextures[1];
		}
		if (value == 13)
		{
			material.m_Flags = MaterialFlags::DoubleSided;
		}
		if (value == 17)
		{
			material.m_AlphaMode = AlphaMode::Blend;
		}
		if (value == 22)
		{
			material.m_AlphaMode = AlphaMode::Mask;
		}
		m_MaterialCase = value;
	}

	void GIReferenceLabSession::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		if (GetParameters().Get(FixtureId, int32_t(0)) >= 14 && feedback.m_SubmittedFence.IsValid())
		{
			if (m_FirstProbeFrame == 0)
			{
				m_FirstProbeFrame = feedback.m_RenderFrameSerial;
			}
			m_LastProbeFrame = feedback.m_RenderFrameSerial;
		}
		if (!m_RayScene || GetParameters().Get(FixtureId, int32_t(0)) != 9)
		{
			return;
		}
		const auto& scene = m_RayScene->GetDiagnostics();
		if (scene.m_Revision == m_LoggedSnapshot)
		{
			return;
		}
		m_LoggedSnapshot = scene.m_Revision;
		GGLAB_LOG_GRAPHICS_INFO("GI material snapshot: case={}, revision={}, geometry={}, blas={}, tlas={}, entity={}, blend={}.",
			m_MaterialCase, scene.m_Revision, scene.m_GeometryRevision, scene.m_BottomLevelBuildCount,
			scene.m_TopLevelBuildCount, entt::to_integral(m_MaterialWall), scene.m_ExcludedBlendCount);
		for (const auto& input : scene.m_MaterialTextures)
		{
			GGLAB_LOG_GRAPHICS_INFO("GI material texture: revision={}, material={}, slot={}, id={}, generation={}, resource={}:{}, srv={}, format={}, width={}, height={}, levels={}, mip={}, source={}, artifact={}.",
				scene.m_Revision, input.m_MaterialIndex, input.m_Slot, input.m_Content.m_Id.Value(),
				input.m_Content.m_Generation, input.m_Resource.Index(), input.m_Resource.Generation(),
				input.m_SrvIndex, uint32_t(input.m_Format), input.m_Width, input.m_Height,
				input.m_MipLevels, input.m_SelectedMip, SourceDigestText(input.m_SourceDigest, 32),
				ArtifactContentDigestText(input.m_ArtifactContentDigest, 32));
		}
		GGLAB_UNUSED(feedback);
	}

	bool GIReferenceLabSession::SupportsImmediateParameter(const LabParameterId& id) const noexcept
	{
		return (id == ProbeTransformId && GetParameters().Get(FixtureId, int32_t(0)) >= 14) ||
			(id == MaterialCaseId && GetParameters().Get(FixtureId, int32_t(0)) == 9) ||
			(id == VisibilityMaterialId && GetParameters().Get(FixtureId, int32_t(0)) == 10);
	}

	std::optional<LabValue> GIReferenceLabSession::ReadImmediateParameterFrameValue(
		const LabParameterId& id, const LabParameterFrameContext& frame) const noexcept
	{
		if (id == ProbeTransformId && frame.m_Scene && frame.m_FrameSerial != 0)
		{
			const auto& registry = m_World.GetRegistry();
			const auto* component = registry.try_get<components::ModelComponent>(m_ProbeEntity);
			const auto* model = component ? m_Services.m_AssetManager->GetModel(component->m_ModelId) : nullptr;
			if (!model || model->m_MeshInstance.empty())
			{
				return std::nullopt;
			}
			for (uint32_t value = 0; value <= 2; ++value)
			{
				const Matrix translation = math::CreateTranslation(Vector3(0.1f * static_cast<float>(value), 0, 0));
				const bool matched = std::ranges::all_of(model->m_MeshInstance, [&](const auto& source)
					{
						const auto* mesh = m_Services.m_AssetManager->GetMesh(source.m_MeshId);
						if (!mesh || !mesh->m_HasBounds)
						{
							return false;
						}
						const Vector3 expected = math::TransformPoint(mesh->m_Aabb.m_Center,
							source.m_LocalTransform * translation);
						return std::ranges::any_of(frame.m_Scene->m_RenderInstances, [&](const auto& instance)
							{
								return instance.m_MeshId == source.m_MeshId &&
									(instance.m_WorldCenterPos - expected).Length() < 0.0001f;
							});
					});
				if (matched)
				{
					return LabValue(value);
				}
			}
			return std::nullopt;
		}
		if ((id != MaterialCaseId && id != VisibilityMaterialId) || !frame.m_Scene || frame.m_FrameSerial == 0)
		{
			return std::nullopt;
		}
		// Read the frozen material identity actually assigned to this frame's table;
		// authoring parameter storage is not evidence of consumption.
		const auto count = id == VisibilityMaterialId ? VisibilityMaterialCount : MaterialCaseCount;
		for (uint32_t value = 0; value < count; ++value)
		{
			const auto key = RenderMaterialKey::FromRuntime(id == VisibilityMaterialId ?
				VisibilityMaterialKey(value) : MaterialCaseKey(value));
			if (std::ranges::any_of(frame.m_Scene->m_RenderInstances, [key](const auto& instance)
				{
					return instance.m_MaterialKey == key;
				}))
			{
				return LabValue(value);
			}
		}
		return std::nullopt;
	}

	void GIReferenceLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const bool probe = GetParameters().Get(FixtureId, int32_t(0)) >= 14;
		const bool allocations = GetParameters().Get(ProbeDiagnosticsId, true);
		std::map<uint64_t, uint32_t> ordinals;
		const auto allocation = [&](const char* owner, size_t index, uint32_t kind, bool active,
			uint64_t logical, const RHIAllocationFacts& facts)
			{
				int64_t ordinal = -1;
				if (facts.m_Status == RHIAllocationFactsStatus::Known)
				{
					const auto [found, inserted] = ordinals.emplace(facts.m_NativeAllocationId,
						static_cast<uint32_t>(ordinals.size()));
					ordinal = found->second;
					GGLAB_UNUSED(inserted);
				}
				diagnostics.m_Metrics.push_back({ std::format("{} allocation {}", owner, index), std::format(
					"kind={};active={};logical={};facts={};allocated={};alignment={};ordinal={}",
					kind, active ? 1 : 0, logical, uint32_t(facts.m_Status), facts.m_AllocatedBytes,
					facts.m_AlignmentBytes, ordinal) });
			};
		if (probe)
		{
			diagnostics.m_Title = "Probe update cost experiment";
			diagnostics.m_Metrics.push_back({ "Activation GPU frame", std::to_string(m_ActivationGpuFrame) });
			diagnostics.m_Metrics.push_back({ "Initialization GPU", std::format("frame={};milliseconds={}",
				m_InitializationGpuFrame, m_InitializationGpuMilliseconds) });
			for (const auto& [name, value] : m_ActivationGpu)
			{
				diagnostics.m_Metrics.push_back({ "Activation GPU " + name,
					std::format("calls={};milliseconds={}", value.first, value.second) });
			}
			if (m_ProbeUpdate)
			{
				const auto progress = m_ProbeUpdate->GetDiagnostics(allocations);
				diagnostics.m_Metrics.push_back({ "Probe progress", std::format(
					"submitted={};completed={};probes={};rayCapacity={};fieldLogical={};prepareMs={};"
					"allocationMs={};shaderLookupMs={};pipelineMs={};initialized={};failed={}",
					progress.m_SubmittedUpdates, progress.m_CompletedUpdates, progress.m_ProbeCount,
					progress.m_RayCapacityBytes, progress.m_FieldLogicalBytes, progress.m_PreparationMilliseconds,
					progress.m_AllocationMilliseconds, progress.m_ShaderLookupMilliseconds,
					progress.m_PipelineMilliseconds,
					progress.m_Initialized ? 1 : 0, progress.m_Failed ? 1 : 0) });
				diagnostics.m_Metrics.push_back({ "Probe inputs", std::format(
					"lightingGeneration={};environmentGeneration={};atmosphereAttenuation={}",
					progress.m_WorldLightingGeneration, progress.m_EnvironmentGeneration,
					progress.m_AtmosphereAttenuation ? 1 : 0) });
				for (size_t index = 0; index < progress.m_Allocations.size(); ++index)
				{
					const uint64_t logical = index == 0 ? progress.m_RayCapacityBytes :
						uint64_t(progress.m_ProbeCount) * (index == 1 ? 1024u : index == 2 ? 2048u : 16u);
					allocation("Probe", index, static_cast<uint32_t>(index), true, logical, progress.m_Allocations[index]);
				}
			}
			if (allocations)
			{
				TransientResourcePoolSnapshot pool;
				m_Services.m_RenderServices.m_Presentation->CaptureTransientResourcePool(pool);
				diagnostics.m_Metrics.push_back({ "Transient inventory", std::format(
					"available={};buffers={};textures={};cacheLimit={}", pool.m_SourceAvailable ? 1 : 0,
					pool.m_BufferCounts.m_Total, pool.m_TextureCounts.m_Total, pool.m_MaxCachedPerKey) });
				auto* device = m_Services.m_RenderServices.m_Presentation->GetDevice();
				size_t index = 0;
				for (const auto& slot : pool.m_Buffers)
				{
					if (slot.m_State != TransientPoolSlotState::Destroyed && slot.m_LogicalName == "RayScene.BuildScratch")
					{
						const auto desc = device->QueryBufferDescription(slot.m_Buffer);
						allocation("Scratch", index++, uint32_t(slot.m_State), true,
							desc ? desc->m_SizeInBytes : 0, device->QueryBufferAllocationFacts(slot.m_Buffer));
					}
				}
			}
		}
		if (!m_RayScene)
		{
			return;
		}
		const auto& scene = m_RayScene->GetDiagnostics();
		if (!probe)
		{
			diagnostics.m_Title = "GI reference snapshot inputs";
		}
		if (probe || GetParameters().Get(FixtureId, int32_t(0)) == 9)
		{
			const auto& cpu = scene.m_Preparation;
			diagnostics.m_Metrics.push_back({ "RayScene preparation", std::format(
				"calls={};replacements={};totalMs={};lastMs={};gatherMs={};planMs={};allocateMs={};lastReplacementMs={};replacementGatherMs={};replacementPlanMs={};replacementAllocateMs={}",
				cpu.m_Calls, cpu.m_Replacements, cpu.m_TotalMilliseconds, cpu.m_LastMilliseconds,
				cpu.m_LastGatherMilliseconds, cpu.m_LastPlanMilliseconds, cpu.m_LastAllocationMilliseconds,
				cpu.m_LastReplacementMilliseconds, cpu.m_ReplacementGatherMilliseconds,
				cpu.m_ReplacementPlanMilliseconds, cpu.m_ReplacementAllocationMilliseconds) });
			if (allocations)
			{
				const auto& overlap = m_RayScene->GetReplacementMemorySnapshot();
				diagnostics.m_Metrics.push_back({ "Replacement ray snapshots", std::to_string(overlap.m_LiveSnapshots) });
				uint64_t overlapBytes = 0;
				uint64_t inactiveBytes = 0;
				bool overlapKnown = !overlap.m_Resources.empty();
				for (const auto& resource : overlap.m_Resources)
				{
					overlapKnown &= resource.m_Allocation.m_Status == RHIAllocationFactsStatus::Known;
					overlapBytes += resource.m_Allocation.m_AllocatedBytes;
					if (!resource.m_Active)
					{
						inactiveBytes += resource.m_Allocation.m_AllocatedBytes;
					}
				}
				diagnostics.m_Metrics.push_back({ "Replacement ray memory", std::format(
					"known={};allocated={};inactive={}", overlapKnown ? 1 : 0, overlapBytes, inactiveBytes) });
				const auto memory = m_RayScene->GetMemorySnapshot();
				diagnostics.m_Metrics.push_back({ "Live ray snapshots", std::to_string(memory.m_LiveSnapshots) });
				for (size_t index = 0; index < memory.m_Resources.size(); ++index)
				{
					const auto& resource = memory.m_Resources[index];
					allocation("RayScene", index, uint32_t(resource.m_Kind), resource.m_Active,
						resource.m_LogicalBytes, resource.m_Allocation);
				}
			}
		}
		for (const auto& [name, value] : std::array<std::pair<const char*, uint64_t>, 7>{ {
			{ "Snapshot revision", scene.m_Revision }, { "Geometry revision", scene.m_GeometryRevision },
			{ "BLAS builds", scene.m_BottomLevelBuildCount }, { "TLAS builds", scene.m_TopLevelBuildCount },
			{ "Material case", m_MaterialCase }, { "Material wall entity", entt::to_integral(m_MaterialWall) },
			{ "Excluded BLEND entries", scene.m_ExcludedBlendCount } } })
		{
			diagnostics.m_Metrics.push_back({ name, std::to_string(value) });
		}
		for (size_t index = 0; index < scene.m_MaterialTextures.size(); ++index)
		{
			const auto& input = scene.m_MaterialTextures[index];
			diagnostics.m_Metrics.push_back({ std::format("Material texture {}", index), std::format(
				"material={};slot={};id={};generation={};resource={}:{};srv={};format={};width={};height={};levels={};mip={}",
				input.m_MaterialIndex, input.m_Slot, input.m_Content.m_Id.Value(), input.m_Content.m_Generation,
				input.m_Resource.Index(), input.m_Resource.Generation(), input.m_SrvIndex, uint32_t(input.m_Format),
				input.m_Width, input.m_Height, input.m_MipLevels, input.m_SelectedMip) });
		}
	}

	void GIReferenceLabSession::TickPrepare() noexcept
	{
		m_Progress = m_Assets.BuildProgress(*m_Services.m_AssetManager, "Preparing GI reference fixture");
		UpdateMaterialProgress();
	}
	void GIReferenceLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT(m_Progress.IsReady());
	}
	void GIReferenceLabSession::CancelPrepare() noexcept
	{
		m_Assets.Reset();
	}
	void GIReferenceLabSession::OnEnter() noexcept
	{
		m_LightingOverride = m_Services.m_EnvironmentLightingControl->AcquireOverride();
		GGLAB_ASSERT(m_LightingOverride);
		const auto fixture = GetParameters().Get(FixtureId, int32_t(0));
		if (fixture >= 14 && GetParameters().Get(ProbeDiagnosticsId, true) &&
			m_Services.m_GpuProfiling && m_Services.m_GpuProfilingControl)
		{
			m_PreviousProbeProfiling = m_Services.m_GpuProfiling->IsEnabled();
			m_RestoreProbeProfiling = true;
			m_Services.m_GpuProfilingControl->RequestEnabled(true);
		}
		const bool coastal = fixture == 11 || fixture == 12 || fixture == 15;
		const bool contract = fixture >= 5;
		const auto environment = coastal ?
			3 : contract ? GetParameters().Get(EnvironmentId, int32_t(0)) : 0;
		m_LightingOverride->Apply({
			.m_Intensity = coastal ?
				1.0f : environment == 0 ? 0.0f : static_cast<float>(GetParameters().Get(IntensityId, int32_t(1))),
			.m_RotationRadians = environment == 0 || coastal ?
				0.0f : static_cast<float>(GetParameters().Get(YawId, int32_t(0))) * 0.017453292519943295f,
			.m_EnableSkybox = coastal,
			.m_BackgroundMode = environment == 3 ?
				EnvironmentBackgroundMode::PhysicalSky : EnvironmentBackgroundMode::TextureEnvironment });
		if (environment == 1 || environment == 2)
		{
			auto& controller = *m_Services.m_EnvironmentAssetController;
			if (const auto* previous = controller.GetActiveEnvironment())
			{
				m_PreviousEnvironmentFile = previous->m_Path;
			}
			m_SelectedEnvironment = true;
			const bool accepted = controller.SelectEnvironmentFile(environment == 1 ?
				"Assets/Textures/GIReference/Constant.hdr" : "Assets/Textures/GIReference/Directional.hdr");
			GGLAB_ASSERT_MSG(accepted, "GI reference contract environment must be available.");
		}
	}
	void GIReferenceLabSession::OnExit() noexcept
	{
		if (m_RestoreProbeProfiling)
		{
			m_Services.m_GpuProfilingControl->RequestEnabled(m_PreviousProbeProfiling);
			m_RestoreProbeProfiling = false;
		}
		if (m_SelectedEnvironment)
		{
			auto& controller = *m_Services.m_EnvironmentAssetController;
			if (m_PreviousEnvironmentFile.empty())
			{
				controller.Reset();
			}
			else
			{
				GGLAB_UNUSED(controller.SelectEnvironmentFile(m_PreviousEnvironmentFile));
			}
			m_SelectedEnvironment = false;
		}
		m_LightingOverride.reset();
	}

	void GIReferenceLabSession::Update(float deltaTime) noexcept
	{
		if (m_RestoreProbeProfiling && (m_ActivationGpuFrame == 0 || m_InitializationGpuFrame == 0) &&
			m_FirstProbeFrame != 0)
		{
			const auto frame = m_Services.m_GpuProfiling->GetLatestFrame();
			if (frame.IsValid() && frame.m_RenderFrameSerial >= m_FirstProbeFrame &&
				frame.m_RenderFrameSerial <= m_LastProbeFrame &&
				m_ActivationGpuFrame == 0 && std::ranges::any_of(frame.m_Samples, [](const auto& sample)
					{
						return sample.m_Name == "RayScene.BuildTLAS";
					}))
			{
				m_ActivationGpuFrame = frame.m_RenderFrameSerial;
				for (const auto& sample : frame.m_Samples)
				{
					if (sample.m_Kind == GpuProfileScopeKind::RenderGraphPass &&
						(sample.m_Name.starts_with("RayScene.") || sample.m_Name == "GI.ProbeUpdate.Initialize"))
					{
						auto& value = m_ActivationGpu[sample.m_Name];
						value.first += sample.m_CallCount;
						value.second += sample.m_Milliseconds;
					}
				}
			}
			if (frame.IsValid() && frame.m_RenderFrameSerial >= m_FirstProbeFrame &&
				frame.m_RenderFrameSerial <= m_LastProbeFrame && m_InitializationGpuFrame == 0)
			{
				for (const auto& sample : frame.m_Samples)
				{
					if (sample.m_Kind == GpuProfileScopeKind::RenderGraphPass &&
						sample.m_Name == "GI.ProbeUpdate.Initialize")
					{
						m_InitializationGpuFrame = frame.m_RenderFrameSerial;
						m_InitializationGpuMilliseconds += sample.m_Milliseconds;
					}
				}
			}
		}
		GGLAB_UNUSED(deltaTime);
	}
	LabId GIReferenceLabSession::GetId() noexcept { return LabId("gglab.lab.gi_reference"); }
	LabDescriptor GIReferenceLabSession::GetDescriptor() noexcept
	{
		return { .m_Id = GetId(), .m_DisplayName = "GI Reference", .m_Category = "Lighting",
			.m_Description = "Bounded fixed-depth irradiance reference: frozen F1-F4 and independent sun/sky contracts.",
			.m_Kind = LabKind::Scene, .m_SchemaVersion = 1 };
	}
	std::unique_ptr<LabSessionBase> GIReferenceLabSession::Create(const LabSessionCreateInfo& info) noexcept
	{
		return std::make_unique<GIReferenceLabSession>(info);
	}
}
