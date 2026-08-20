#include "Application/Lab/Sessions/SurfaceProbeLabSession.h"
#include "Core/Math/Quaternion.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Asset/ReservedTexture.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/SamplerRegistry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	namespace
	{
		// surface_probe.base_color_factor fixtures: two concrete, documented RGB
		// factors so the A/B surface difference is deterministic.
		enum class SurfaceFactorFixture : int32_t
		{
			DarkGray = 0,
			Orange = 1,
		};

		// surface_probe.base_color_texture fixtures: two deterministic bootstrap
		// textures registered through the normal texture asset path.
		enum class SurfaceTextureFixture : int32_t
		{
			White = 0,
			Checker = 1,
		};

		const Color SurfaceFactorFixtures[2] = {
			Color(0.10f, 0.10f, 0.10f, 1.0f),
			Color(0.90f, 0.28f, 0.10f, 1.0f),
		};

		const ReservedTextureIDIndex SurfaceTextureFixtures[2] = {
			ReservedTextureIDIndex::BaseColorWhite,
			ReservedTextureIDIndex::MissingTextureChecker,
		};

		const LabParameterId BaseColorFactorId("surface_probe.base_color_factor");
		const LabParameterId BaseColorTextureId("surface_probe.base_color_texture");
		const LabParameterId DebugViewId("surface_probe.material_debug_view");
		const LabParameterId EnableCameraInputId("surface_probe.camera.enable_input");

		[[nodiscard]] int ClampFixtureIndex(int32_t value) noexcept
		{
			return std::clamp(value, 0, 1);
		}

		[[nodiscard]] Color ResolveFactorFixture(int32_t fixture) noexcept
		{
			return SurfaceFactorFixtures[ClampFixtureIndex(fixture)];
		}

		[[nodiscard]] TextureID ResolveTextureFixture(int32_t fixture) noexcept
		{
			return ToTextureId(SurfaceTextureFixtures[ClampFixtureIndex(fixture)]);
		}

		components::MaterialInstanceComponent MakeProbeMaterial() noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey("gglab.lab.surface_probe.primitive");
			material.m_Properties.m_BaseColor = SurfaceFactorFixtures[0];
			material.m_Properties.m_BaseColorBinding.m_TextureId =
				ToTextureId(SurfaceTextureFixtures[0]);
			material.m_Properties.m_RoughnessFactor = 1.0f;
			material.m_Properties.m_DebugView = MaterialDebugView::Lit;
			return material;
		}
	}

	SurfaceProbeLabSession::SurfaceProbeLabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>())
	{
		auto& profile = GetMutableViewRenderProfile();
		profile.m_Lighting.m_ForwardPlus.m_Mode = ForwardLightingMode::Legacy;
		profile.m_Lighting.m_GTAO.m_Enabled = false;
		profile.m_PostProcess.m_Bloom.m_Enabled = false;

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BaseColorFactorId,
			.m_Name = "Base Color Factor",
			.m_Group = "Surface Probe",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(SurfaceFactorFixture::DarkGray),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(SurfaceFactorFixture::DarkGray),
						.m_Name = "A - Dark Gray (0.10, 0.10, 0.10, 1.0)",
					},
					{
						.m_Value = int32_t(SurfaceFactorFixture::Orange),
						.m_Name = "B - Orange (0.90, 0.28, 0.10, 1.0)",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = BaseColorTextureId,
			.m_Name = "Base Color Texture",
			.m_Group = "Surface Probe",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(SurfaceTextureFixture::White),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(SurfaceTextureFixture::White),
						.m_Name = "A - BaseColorWhite (1x1 solid white)",
					},
					{
						.m_Value = int32_t(SurfaceTextureFixture::Checker),
						.m_Name = "B - MissingTextureChecker (64x64 8px purple/black checker)",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = DebugViewId,
			.m_Name = "Material Debug View",
			.m_Group = "Surface Probe",
			.m_Type = LabParameterType::Enum,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(MaterialDebugView::Lit),
			.m_EnumItems =
				{
					{
						.m_Value = int32_t(MaterialDebugView::Lit),
						.m_Name = "Lit (existing Forward PBR lighting)",
					},
					{
						.m_Value = int32_t(MaterialDebugView::BaseColor),
						.m_Name = "Base Color (unlit, surface output only)",
					},
				},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId,
			.m_Name = "Enable Camera Input",
			.m_Group = "Camera",
			.m_Type = LabParameterType::Bool,
			.m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		ApplyImmediateParameters();
	}

	void SurfaceProbeLabSession::BeginPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		RebuildScene();
		m_LoadingProgress =
			m_AssetPreparation.BuildProgress(*m_Services.m_AssetManager, "Preparing Surface Probe");
	}

	void SurfaceProbeLabSession::TickPrepare() noexcept
	{
		if (!m_LoadingProgress.IsPreparing())
		{
			return;
		}
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager, "Preparing Surface Probe");
	}

	void SurfaceProbeLabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(),
			"Surface Probe Lab committed before its assets were ready.");
	}

	void SurfaceProbeLabSession::CancelPrepare() noexcept
	{
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_ProbeEntity = entt::null;
		m_FixtureConfigured = false;
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void SurfaceProbeLabSession::Update(float deltaTime) noexcept
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

	void SurfaceProbeLabSession::ApplyImmediateParameters() noexcept
	{
		m_EnableCameraInput = GetParameters().Get(EnableCameraInputId, false);
		ApplyProbeFixtures();
	}

	void SurfaceProbeLabSession::RebuildScene() noexcept
	{
		ResetAssetInterests();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		m_ProbeEntity = entt::null;
		m_FixtureConfigured = false;
		ApplyCameraPreset();

		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.6f);

		components::TransformComponent transform{};
		transform.m_Position = Vector3(0.0f, 0.75f, -6.5f);
		transform.m_Scale = Vector3::One * 2.5f;
		m_ProbeEntity = primitive::Cube::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World,
			.m_Transform = transform,
			.m_MaterialInstance = MakeProbeMaterial(),
			});

		m_FixtureConfigured = registry.valid(m_ProbeEntity) &&
			registry.all_of<components::TransformComponent, components::ModelComponent,
				components::MaterialInstanceComponent>(m_ProbeEntity);

		BuildLighting();
		ApplyProbeFixtures();
	}

	void SurfaceProbeLabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	// Applies the named probe fixtures to the material instance through the
	// normal runtime material update path: the per-frame RenderSceneBuilder
	// re-encodes MaterialProperties into MaterialGPU (factor plus the
	// base-color texture binding) and uploads the changed material table slot.
	void SurfaceProbeLabSession::ApplyProbeFixtures() noexcept
	{
		const auto& parameters = GetParameters();
		const int32_t factorFixture =
			parameters.Get(BaseColorFactorId, int32_t(SurfaceFactorFixture::DarkGray));
		const int32_t textureFixture =
			parameters.Get(BaseColorTextureId, int32_t(SurfaceTextureFixture::White));
		const MaterialDebugView debugView =
			parameters.Get(DebugViewId, int32_t(MaterialDebugView::Lit)) ==
			int32_t(MaterialDebugView::BaseColor)
			? MaterialDebugView::BaseColor
			: MaterialDebugView::Lit;

		auto& registry = m_World.GetRegistry();
		if (!registry.valid(m_ProbeEntity))
		{
			return;
		}

		components::MaterialInstanceComponent& material =
			registry.get<components::MaterialInstanceComponent>(m_ProbeEntity);
		material.m_Properties.m_BaseColor = ResolveFactorFixture(factorFixture);
		material.m_Properties.m_BaseColorBinding.m_TextureId =
			ResolveTextureFixture(textureFixture);
		material.m_Properties.m_DebugView = debugView;
	}

	bool SurfaceProbeLabSession::ProbeMaterialMatchesFixtures(const MaterialProperties& properties,
		int32_t factorIndex, int32_t textureIndex, MaterialDebugView debugView) const noexcept
	{
		const Color expectedFactor = ResolveFactorFixture(factorIndex);
		const TextureID expectedTexture = ResolveTextureFixture(textureIndex);
		return properties.m_BaseColor.m_R == expectedFactor.m_R &&
			properties.m_BaseColor.m_G == expectedFactor.m_G &&
			properties.m_BaseColor.m_B == expectedFactor.m_B &&
			properties.m_BaseColor.m_A == expectedFactor.m_A &&
			properties.m_BaseColorBinding.m_TextureId == expectedTexture &&
			properties.m_DebugView == debugView;
	}

	void SurfaceProbeLabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity lightEntity = registry.create();

		components::TransformComponent transform{};
		Vector3 direction(-0.35f, -0.82f, -0.45f);
		direction.Normalize();
		transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(lightEntity, transform);

		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Intensity = 3.0f;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(lightEntity, light);
	}

	void SurfaceProbeLabSession::ApplyCameraPreset() noexcept
	{
		GetCamera().LookAt(Vector3(0.0f, 1.0f, -9.0f), Vector3(0.0f, 0.8f, 0.0f));
		GetCamera().SetFov(50.0f);
		GetCamera().SetNearFar(0.1f, 250.0f);
		GetCamera().Update();
	}

	void SurfaceProbeLabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		diagnostics.m_Title = "Surface Probe (gglab.surface)";

		const auto& parameters = GetParameters();
		const int32_t factorFixture =
			parameters.Get(BaseColorFactorId, int32_t(SurfaceFactorFixture::DarkGray));
		const int32_t textureFixture =
			parameters.Get(BaseColorTextureId, int32_t(SurfaceTextureFixture::White));
		const int32_t factorIndex = ClampFixtureIndex(factorFixture);
		const int32_t textureIndex = ClampFixtureIndex(textureFixture);
		const MaterialDebugView debugView =
			parameters.Get(DebugViewId, int32_t(MaterialDebugView::Lit)) ==
			int32_t(MaterialDebugView::BaseColor)
			? MaterialDebugView::BaseColor
			: MaterialDebugView::Lit;
		const Color factor = SurfaceFactorFixtures[factorIndex];

		std::string factorName = factorIndex == int32_t(SurfaceFactorFixture::Orange)
			? "B - Orange (0.90, 0.28, 0.10, 1.0)"
			: "A - Dark Gray (0.10, 0.10, 0.10, 1.0)";
		std::string textureName = textureIndex == int32_t(SurfaceTextureFixture::Checker)
			? "B - MissingTextureChecker (64x64 8px purple/black checker)"
			: "A - BaseColorWhite (1x1 solid white)";

		std::string expected = debugView == MaterialDebugView::BaseColor
			? (textureIndex == int32_t(SurfaceTextureFixture::Checker)
				? "Unlit base-color view: 8px purple/black checkerboard tiles across each face, "
				  "tinted by the active base color factor."
				: "Unlit base-color view: uniform surface tinted by the active base color factor "
				  "(factor x white texture).")
			: (textureIndex == int32_t(SurfaceTextureFixture::Checker)
				? "Lit Forward PBR: 8px purple/black checkerboard tiles shading under the "
				  "directional light, tinted by the active base color factor."
				: "Lit Forward PBR: uniform surface shading under the directional light, tinted "
				  "by the active base color factor (factor x white texture).");

		// Mechanical CPU-side check: the material instance (the source of the
		// normal runtime material update path) must carry the active fixtures.
		bool fixturesApplied = false;
		if (m_World.GetRegistry().valid(m_ProbeEntity))
		{
			const components::MaterialInstanceComponent& material =
				m_World.GetRegistry().get<components::MaterialInstanceComponent>(m_ProbeEntity);
			fixturesApplied = ProbeMaterialMatchesFixtures(
				material.m_Properties, factorIndex, textureIndex, debugView);
		}

		diagnostics.m_Metrics = {
			{
				.m_Name = "Base color factor fixture",
				.m_Value = factorName,
			},
			{
				.m_Name = "Factor RGB",
				.m_Value = std::format("({:.2f}, {:.2f}, {:.2f})", factor.m_R, factor.m_G,
					factor.m_B),
			},
			{
				.m_Name = "Base color texture fixture",
				.m_Value = textureName,
			},
			{
				.m_Name = "Material debug view",
				.m_Value =
					debugView == MaterialDebugView::BaseColor ? "Base Color (unlit)" : "Lit",
			},
			{
				.m_Name = "Expected surface result",
				.m_Value = expected,
			},
		};
		diagnostics.m_Checks = {
			{
				.m_Name = "Fixture construction",
				.m_Status = !m_LoadingProgress.IsReady() ? LabDiagnosticCheckStatus::Pending
					: m_FixtureConfigured ? LabDiagnosticCheckStatus::Passed
										  : LabDiagnosticCheckStatus::Failed,
				.m_Detail =
					"The probe primitive carries transform, model, and material instance components.",
			},
			{
				.m_Name = "Surface fixtures applied",
				.m_Status = !m_LoadingProgress.IsReady() || !m_FixtureConfigured
					? LabDiagnosticCheckStatus::Pending
					: fixturesApplied ? LabDiagnosticCheckStatus::Passed
									 : LabDiagnosticCheckStatus::Failed,
				.m_Detail =
					"The material instance holds the active base color factor and texture binding; "
					"the renderer re-encodes it into the runtime material table each frame.",
			},
		};
	}

	LabId SurfaceProbeLabSession::GetId() noexcept
	{
		return LabId("gglab.lab.surface_probe");
	}

	LabDescriptor SurfaceProbeLabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(),
			.m_DisplayName = "Surface Probe",
			.m_Category = "Materials",
			.m_Description =
				"Probes the gglab.surface surface evaluation seam with named A/B base color "
				"factor and base color texture fixtures driven through the normal runtime "
				"material path on the existing Forward PBR lighting.",
			.m_Kind = LabKind::Pipeline,
			.m_SchemaVersion = 1,
		};
	}

	std::unique_ptr<LabSessionBase> SurfaceProbeLabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<SurfaceProbeLabSession>(createInfo);
	}
}
