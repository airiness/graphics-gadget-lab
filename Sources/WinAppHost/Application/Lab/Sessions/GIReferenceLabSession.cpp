#include "Application/Lab/Sessions/GIReferenceLabSession.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/EnvironmentAssetController.h"
#include "GGLabRuntime/Graphics/Atmosphere.h"
#include "GGLabRuntime/Graphics/Geometry.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace gglab
{
	namespace
	{
		const LabParameterId FixtureId("gi_reference.fixture");
		const LabParameterId SamplesId("gi_reference.samples");
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

		class GIReferencePipeline final : public RenderPipelineBase
		{
		public:
			GIReferencePipeline(World& world, AssetManager& assets, GIReferenceCreateInfo info) noexcept :
				m_World(world), m_Assets(assets), m_Scene(info.m_RayScene),
				m_Raster(CreateRenderPipelineForwardPlus({ .m_GIReference = std::move(info) })) {}
			std::string_view GetName() const noexcept override { return "GI one-bounce reference"; }
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
				{ 7, "Contract: published environment sphere" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SamplesId, .m_Name = "Samples per pixel", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(64), .m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(4096)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SeedId, .m_Name = "Independent seed", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(61051) }));
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
		const auto fixture = GetParameters().Get(FixtureId, int32_t(0));
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
		if (fixture == 0 || fixture == 7)
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
		else if (fixture == 5 || fixture == 6)
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
		const auto environment = contract ? GetParameters().Get(EnvironmentId, int32_t(0)) : 0;
		if (fixture == 4 || fixture == 5 || fixture == 6 || environment == 3)
		{
			auto& registry = m_World.GetRegistry();
			const auto light = registry.create();
			Vector3 direction = fixture == 4 ? Vector3(0.2f, -1, -1) : Vector3(0, -1, 1);
			direction.Normalize();
			components::TransformComponent transform;
			transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
			registry.emplace<components::TransformComponent>(light, transform);
			components::LightComponent sun;
			sun.m_WorldSun = WorldSunSettings{
				.m_PerpendicularIlluminanceLux = fixture == 4 ? 10000.0f :
					static_cast<float>(GetParameters().Get(SunId, int32_t(10000))) };
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
		GIReferenceCreateInfo reference{ .m_RayScene = std::make_shared<RayScene>(),
			.m_SampleCount = GetParameters().Get(SamplesId, uint32_t(64)),
			.m_Seed = GetParameters().Get(SeedId, uint32_t(61051)) };
		reference.m_ConstantEnvironment = environment == 0;
		// The HDR importer decodes RGBE bin centres: (128 + 0.5) / 128.
		// Match that exact constant in the independent direct-input contract.
		const float radiance = fixture == 4 ? 0.1f :
			fixture == 5 || fixture == 6 ? 0.0f :
				fixture == 7 ? 1.00390625f : 1.0f;
		reference.m_ConstantRadiance = { radiance, radiance, radiance };
		reference.m_EnvironmentFilter = contract && GetParameters().Get(FilterId, int32_t(0)) == 1 ?
			GIReferenceEnvironmentFilter::MipZero : GIReferenceEnvironmentFilter::Face32;
		SetRenderPipeline(std::make_unique<GIReferencePipeline>(m_World,
			*m_Services.m_AssetManager, std::move(reference)));
		m_World.MarkLightingAuthoringChanged();
		m_Progress = m_Assets.BuildProgress(*m_Services.m_AssetManager, "Preparing GI reference fixture");
	}

	void GIReferenceLabSession::TickPrepare() noexcept
	{
		m_Progress = m_Assets.BuildProgress(*m_Services.m_AssetManager, "Preparing GI reference fixture");
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
		const bool contract = GetParameters().Get(FixtureId, int32_t(0)) >= 5;
		const auto environment = contract ? GetParameters().Get(EnvironmentId, int32_t(0)) : 0;
		m_LightingOverride->Apply({
			.m_Intensity = environment == 0 ? 0.0f : static_cast<float>(GetParameters().Get(IntensityId, int32_t(1))),
			.m_RotationRadians = environment == 0 ? 0.0f :
				static_cast<float>(GetParameters().Get(YawId, int32_t(0))) * 0.017453292519943295f,
			.m_EnableSkybox = false,
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
		GGLAB_UNUSED(deltaTime);
	}
	LabId GIReferenceLabSession::GetId() noexcept { return LabId("gglab.lab.gi_reference"); }
	LabDescriptor GIReferenceLabSession::GetDescriptor() noexcept
	{
		return { .m_Id = GetId(), .m_DisplayName = "GI Reference", .m_Category = "Lighting",
			.m_Description = "Stateless one-bounce irradiance reference: frozen F1-F4 and independent sun/sky contracts.",
			.m_Kind = LabKind::Scene, .m_SchemaVersion = 1 };
	}
	std::unique_ptr<LabSessionBase> GIReferenceLabSession::Create(const LabSessionCreateInfo& info) noexcept
	{
		return std::make_unique<GIReferenceLabSession>(info);
	}
}
