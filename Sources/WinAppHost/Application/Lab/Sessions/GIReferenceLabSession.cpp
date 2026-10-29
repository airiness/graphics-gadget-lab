#include "Application/Lab/Sessions/GIReferenceLabSession.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/Geometry.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

#include <array>
#include <memory>
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
				{ 0, "F1 isolated sphere" }, { 1, "F1 isolated plane" }, { 2, "F2 sealed thick-wall room" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SamplesId, .m_Name = "Samples per pixel", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(64), .m_MinValue = LabValue(uint32_t(1)), .m_MaxValue = LabValue(uint32_t(4096)) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = SeedId, .m_Name = "Independent seed", .m_Group = "Reference",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = uint32_t(61051) }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = AlbedoId, .m_Name = "F1 albedo variant", .m_Group = "Reference",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(0), .m_EnumItems = { { 0, "White" }, { 1, "Linear (0.2, 0.5, 0.8)" } } }));
		GGLAB_UNUSED(parameters.Add({ .m_Id = ExposureId, .m_Name = "EV100", .m_Group = "Reference",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::RestartSession,
			.m_DefaultValue = int32_t(10), .m_EnumItems = { { 0, "0" }, { 10, "10" }, { 15, "15" } } }));
		auto& profile = GetMutableViewRenderProfile();
		profile.m_TemporalAA.m_Enabled = false;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;
		profile.m_EnableScenePreExposure = true;
	}

	void GIReferenceLabSession::BeginPrepare() noexcept
	{
		m_World.GetRegistry().clear();
		m_Assets.Reset();
		const auto fixture = GetParameters().Get(FixtureId, int32_t(0));
		const float exposure = static_cast<float>(GetParameters().Get(ExposureId, int32_t(10)));
		const Color albedo = fixture == 2 ? Color(0.5f, 0.5f, 0.5f, 1.0f) :
			GetParameters().Get(AlbedoId, int32_t(0)) == 1 ? Color(0.2f, 0.5f, 0.8f, 1.0f) : Color::White;
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
		if (fixture == 0)
		{
			GGLAB_UNUSED(primitive::Sphere::Create(primitiveInfo));
			m_Assets.TrackModel(ProceduralSphereModelID, "F1 sphere", 1.0f);
			views.push_back(View("F1_Sphere", { 0, 0, -4 }, { 0, 0, 0 }, 45.0f, exposure));
		}
		else if (fixture == 1)
		{
			primitiveInfo.m_Transform.m_Scale = Vector3(2.0f, 2.0f, 1.0f);
			primitiveInfo.m_Transform.m_Rotation = math::RotationFromTo(Vector3(0, 0, -1), Vector3::Up);
			GGLAB_UNUSED(primitive::Plane::Create(primitiveInfo));
			m_Assets.TrackModel(ProceduralPlaneModelID, "F1 plane", 1.0f);
			views.push_back(View("F1_Plane", { 0, 3, -3 }, { 0, 0, 0 }, 45.0f, exposure));
		}
		else
		{
			// Frozen specification v1: six closed solids, outward normals, t = 1 m.
			const std::array<std::pair<Vector3, Vector3>, 6> solids
			{
				std::pair{ Vector3(-3, -1, -3), Vector3(3, 0, 3) },
				std::pair{ Vector3(-3, 4, -3), Vector3(3, 5, 3) },
				std::pair{ Vector3(-3, 0, -3), Vector3(-2, 4, 3) },
				std::pair{ Vector3(2, 0, -3), Vector3(3, 4, 3) },
				std::pair{ Vector3(-2, 0, -3), Vector3(2, 4, -2) },
				std::pair{ Vector3(-2, 0, 2), Vector3(2, 4, 3) },
			};
			for (const auto& [minimum, maximum] : solids)
			{
				primitiveInfo.m_Transform.m_Position = (minimum + maximum) * 0.5f;
				primitiveInfo.m_Transform.m_Scale = (maximum - minimum) * 0.5f;
				GGLAB_UNUSED(primitive::Cube::Create(primitiveInfo));
			}
			m_Assets.TrackModel(ProceduralCubeModelID, "F2 closed shell", 1.0f);
			views = {
				View("F2_Interior", { 0, 1.6f, 1.4f }, { 0, 1.6f, -1 }, 60, exposure),
				View("F2_Floor", { 0, 1.6f, 1.4f }, { 0, 0, 0 }, 90, exposure),
				View("F2_Ceiling", { 0, 1.6f, 1.4f }, { 0, 4, 0 }, 90, exposure),
				View("F2_West", { 0, 1.6f, 0 }, { -2, 2, 0 }, 90, exposure),
				View("F2_East", { 0, 1.6f, 0 }, { 2, 2, 0 }, 90, exposure),
				View("F2_Front", { 0, 1.6f, -1.4f }, { 0, 1.6f, 2 }, 60, exposure),
				View("F2_Roof", { 0, 8, -3 }, { 0, 5, 0 }, 60, exposure),
			};
		}
		const auto firstView = views.front().m_Id;
		const bool registered = GetCameraRig().SetReferenceViews(std::move(views));
		const bool restored = GetCameraRig().RestoreReferenceView(firstView);
		GGLAB_ASSERT(registered && restored);
		GIReferenceCreateInfo reference{ .m_RayScene = std::make_shared<RayScene>(),
			.m_SampleCount = GetParameters().Get(SamplesId, uint32_t(64)),
			.m_Seed = GetParameters().Get(SeedId, uint32_t(61051)) };
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
		m_PreviousEnvironment = m_Services.m_EnvironmentLighting->GetEnvironmentLightingSettings();
		m_Entered = true;
		m_Services.m_EnvironmentLightingControl->SetIntensity(0.0f);
		m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(false);
	}
	void GIReferenceLabSession::OnExit() noexcept
	{
		if (m_Entered)
		{
			m_Services.m_EnvironmentLightingControl->SetIntensity(m_PreviousEnvironment.m_Intensity);
			m_Services.m_EnvironmentLightingControl->SetSkyboxEnabled(m_PreviousEnvironment.m_EnableSkybox);
			m_Entered = false;
		}
	}
	void GIReferenceLabSession::Update(float deltaTime) noexcept
	{
		GGLAB_UNUSED(deltaTime);
	}
	LabId GIReferenceLabSession::GetId() noexcept { return LabId("gglab.lab.gi_reference"); }
	LabDescriptor GIReferenceLabSession::GetDescriptor() noexcept
	{
		return { .m_Id = GetId(), .m_DisplayName = "GI Reference", .m_Category = "Lighting",
			.m_Description = "Stateless one-bounce irradiance reference with frozen F1/F2 procedural fixtures.",
			.m_Kind = LabKind::Scene, .m_SchemaVersion = 1 };
	}
	std::unique_ptr<LabSessionBase> GIReferenceLabSession::Create(const LabSessionCreateInfo& info) noexcept
	{
		return std::make_unique<GIReferenceLabSession>(info);
	}
}
