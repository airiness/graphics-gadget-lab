#include "Application/Lab/Sessions/TemporalAALabSession.h"
#include "Application/Lab/Sessions/TemporalAALabDiagnostics.h"
#include "GGLabRuntime/Graphics/RenderHost.h"
#include "AppRuntimeLog.h"

#include "GGLabRuntime/Core/Math/MathFunctions.h"
#include "GGLabRuntime/Core/Math/Quaternion.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Diagnostics/Snapshots/LabSnapshot.h"
#include "GGLabRuntime/Graphics/Asset/AssetManager.h"
#include "GGLabRuntime/Graphics/Camera.h"
#include "GGLabRuntime/Graphics/CameraPath.h"
#include "GGLabRuntime/Graphics/Geometry.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RenderPipeline/RenderPipelineForwardPlus.h"
#include "GGLabRuntime/Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <ranges>
#include <string>
#include <string_view>

namespace gglab
{
	namespace
	{
		constexpr std::string_view AlphaBlendModeTestPath =
			"Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";

		const LabParameterId EnabledId("temporal_aa.enabled");
		const LabParameterId PreviewTapId("temporal_aa.preview_tap");
		const LabParameterId EnableCameraInputId("temporal_aa.camera.enable_input");
		const LabParameterId OrbitCameraId("temporal_aa.camera.orbit");
		const LabParameterId ContinuousFovZoomId("temporal_aa.camera.continuous_fov_zoom");
		const LabParameterId CameraCutSerialId("temporal_aa.camera.cut_serial");
		const LabParameterId AnimateObjectId("temporal_aa.object.animate");
		const LabParameterId MaxHistoryFeedbackId("temporal_aa.max_history_feedback");

		const Vector3 FixtureViewPosition(0.0f, 2.2f, -10.0f);
		const Vector3 FixtureViewTarget(0.0f, 0.0f, 6.5f);
		constexpr float FixtureViewFov = 52.0f;
		constexpr float LightIntensity = 3.5f;
		constexpr std::string_view LightChangePathId = "SEQ_TemporalAALab_LightChange";
		constexpr std::string_view MovingObjectPathId = "SEQ_TemporalAALab_MovingObject";
		// Sequence frames advance the moving-object animation at this rate.
		constexpr float MovingObjectPathFramesPerSecond = 60.0f;

		// Position of the animated rigid object after the given animation time.
		[[nodiscard]] Vector2 ResolveMovingObjectPosition(float seconds) noexcept
		{
			return Vector2(std::sin(seconds * 1.35f) * 3.2f,
				-0.15f + std::sin(seconds * 0.7f) * 0.35f);
		}

		// Directional light of the light-change sequence: converged for 60 frames, a step
		// to 60% at frame 60, then a linear return over frames 120-179.
		[[nodiscard]] constexpr float ResolveLightChangeIntensity(uint32_t frame) noexcept
		{
			constexpr float dimmed = LightIntensity * 0.6f;
			if (frame < 60)
			{
				return LightIntensity;
			}
			if (frame < 120)
			{
				return dimmed;
			}
			const float fade = std::min(static_cast<float>(frame - 120) / 59.0f, 1.0f);
			return dimmed + (LightIntensity - dimmed) * fade;
		}

		components::MaterialInstanceComponent MakeMaterial(std::string_view key,
			const Color& color, float roughness, float metallic = 0.0f,
			const Color& emissive = Color::Black) noexcept
		{
			components::MaterialInstanceComponent material{};
			material.m_Key = RuntimeMaterialKey(key);
			material.m_Properties.m_BaseColor = color;
			material.m_Properties.m_RoughnessFactor = roughness;
			material.m_Properties.m_MetallicFactor = metallic;
			material.m_Properties.m_EmissiveColor = emissive;
			return material;
		}
	}

	TemporalAALabSession::TemporalAALabSession(const LabSessionCreateInfo& createInfo) noexcept :
		LabSessionBase(GetDescriptor(), createInfo, CreateRenderPipelineForwardPlus()),
		m_ViewportWidth(createInfo.m_WindowWidth), m_ViewportHeight(createInfo.m_WindowHeight)
	{
		auto& profile = GetMutableViewRenderProfile();
		profile.m_TemporalAA.m_Enabled = true;
		profile.m_Lighting.m_GTAO.m_Enabled = true;
		profile.m_PostProcess.m_Bloom.m_Enabled = true;

		auto& parameters = GetMutableParameters();
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnabledId, .m_Name = "Enabled", .m_Group = "Temporal AA",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = MaxHistoryFeedbackId,
			.m_Name = "Max History Feedback", .m_Group = "Temporal AA",
			.m_Type = LabParameterType::Float, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = TemporalAADefaultMaxHistoryFeedback,
			.m_MinValue = LabValue(0.0f),
			.m_MaxValue = LabValue(TemporalAAMaxHistoryFeedbackCeiling),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = PreviewTapId, .m_Name = "Preview", .m_Group = "Diagnostics",
			.m_Type = LabParameterType::Enum, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = int32_t(PostProcessDebugTap::TemporalHistoryWeight),
			.m_EnumItems = {
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalHistoryColor), .m_Name = "History Color" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalReprojectionUV), .m_Name = "Reprojection UV" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalRejection), .m_Name = "Rejection" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalHistoryWeight), .m_Name = "History Weight" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalHistorySamples), .m_Name = "History Samples" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalClipDistance), .m_Name = "Clip Distance" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalHistoryRelaxation), .m_Name = "History Relaxation" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalMotionDirection), .m_Name = "Motion Direction" },
				{ .m_Value = int32_t(PostProcessDebugTap::TemporalMotionMagnitude), .m_Name = "Motion Magnitude" },
			},
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = AnimateObjectId, .m_Name = "Animate Rigid Object", .m_Group = "Cases",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = true,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = OrbitCameraId, .m_Name = "Camera Pan / Rotation", .m_Group = "Cases",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = ContinuousFovZoomId, .m_Name = "Continuous FOV Zoom", .m_Group = "Cases",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = CameraCutSerialId, .m_Name = "Camera Cut Serial", .m_Group = "Cases",
			.m_Type = LabParameterType::UInt, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = uint32_t(0), .m_MinValue = LabValue(uint32_t(0)),
			.m_MaxValue = LabValue(uint32_t(1000)),
			}));
		GGLAB_UNUSED(parameters.Add({
			.m_Id = EnableCameraInputId, .m_Name = "Enable Camera Input", .m_Group = "Camera",
			.m_Type = LabParameterType::Bool, .m_Impact = LabChangeImpact::Immediate,
			.m_DefaultValue = false,
			}));
		ApplyImmediateParameters();
	}

	void TemporalAALabSession::BeginPrepare() noexcept
	{
		BuildScene();
		m_LoadingProgress = m_AssetPreparation.BuildProgress(
			*m_Services.m_AssetManager, "Preparing Temporal AA Lab");
	}

	void TemporalAALabSession::TickPrepare() noexcept
	{
		if (m_LoadingProgress.IsPreparing())
		{
			m_LoadingProgress = m_AssetPreparation.BuildProgress(
				*m_Services.m_AssetManager, "Preparing Temporal AA Lab");
		}
	}

	void TemporalAALabSession::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_LoadingProgress.IsReady(),
			"Temporal AA Lab committed before procedural assets were ready.");
	}

	void TemporalAALabSession::CancelPrepare() noexcept
	{
		m_AssetPreparation.Reset();
		m_World.GetRegistry().clear();
		m_MovingEntity = entt::null;
		ResetEvidenceCapture();
		m_LoadingProgress = LoadingProgress::Ready();
	}

	void TemporalAALabSession::OnEnter() noexcept
	{
		auto* profilingView = m_Services.m_GpuProfiling;
		auto* profilingControl = m_Services.m_GpuProfilingControl;
		if (profilingView && profilingControl)
		{
			m_GpuProfilerWasEnabled = profilingView->IsEnabled();
			profilingControl->RequestEnabled(true);
		}
		ResetEvidenceCapture();
		auto* registry = m_Services.m_RenderServices.m_Resources;
		m_PreviousPreviewSelection = registry->GetPostProcessPreviewSelection(
			PostProcessPreviewChannel::TemporalAA);
		m_IsEntered = true;
		ApplySelectedPreviewSelection();
	}

	void TemporalAALabSession::OnExit() noexcept
	{
		m_IsEntered = false;
		if (auto* profilingControl = m_Services.m_GpuProfilingControl)
		{
			profilingControl->RequestEnabled(m_GpuProfilerWasEnabled);
		}
		if (auto* registry = m_Services.m_RenderServices.m_Resources)
		{
			registry->SetPostProcessPreviewSelection(m_PreviousPreviewSelection,
				PostProcessPreviewChannel::TemporalAA);
			registry->RequestPostProcessPreview(PostProcessPreviewChannel::TemporalAA);
		}
	}

	void TemporalAALabSession::Update(float deltaTime) noexcept
	{
		m_ElapsedSeconds += deltaTime;
		auto& registry = m_World.GetRegistry();
		// A light-change sequence frame sets the intensity after this update.
		if (registry.valid(m_LightEntity))
		{
			registry.get<components::LightComponent>(m_LightEntity).m_Intensity = LightIntensity;
		}
		if (m_AnimateObject && registry.valid(m_MovingEntity))
		{
			auto& transform = registry.get<components::TransformComponent>(m_MovingEntity);
			const Vector2 position = ResolveMovingObjectPosition(m_ElapsedSeconds);
			transform.m_Position.m_X = position.m_X;
			transform.m_Position.m_Y = position.m_Y;
		}
		if (m_EnableCameraInput)
		{
			UpdateCamera(deltaTime);
		}
		else if (m_OrbitCamera)
		{
			const float angle = m_ElapsedSeconds * 0.18f;
			const Vector3 eye(std::sin(angle) * 4.0f, 2.2f,
				-10.0f + std::cos(angle) * 2.0f);
			const Vector3 target(0.0f, 0.0f, 6.5f);
			const Vector3 forward = math::SafeNormalize(target - eye, Vector3::Forward);
			GetCamera().SetPosition(eye);
			GetCamera().SetYawPitch(std::atan2(forward.m_X, forward.m_Z),
				std::asin(std::clamp(forward.m_Y, -1.0f, 1.0f)));
			GetCamera().Update();
		}
		else
		{
			GetCamera().Update();
		}
		if (m_ContinuousFovZoom)
		{
			GetCamera().SetFov(52.0f + std::sin(m_ElapsedSeconds * 0.5f) * 12.0f);
			GetCamera().Update();
		}
		RequestPreviewRefresh();
	}

	void TemporalAALabSession::OnCameraPathFrameApplied(
		const CameraPath& path, uint32_t frame) noexcept
	{
		const bool lightChange = path.m_Id == LightChangePathId;
		if (!lightChange && path.m_Id != MovingObjectPathId)
		{
			return;
		}
		auto& registry = m_World.GetRegistry();
		if (lightChange && registry.valid(m_LightEntity))
		{
			registry.get<components::LightComponent>(m_LightEntity).m_Intensity =
				ResolveLightChangeIntensity(frame);
		}
		// The rigid object rests at its start pose while the light changes, and follows
		// the path frame on the moving-object path.
		if (registry.valid(m_MovingEntity))
		{
			const Vector2 position = ResolveMovingObjectPosition(lightChange ? 0.0f
				: static_cast<float>(frame) / MovingObjectPathFramesPerSecond);
			auto& transform = registry.get<components::TransformComponent>(m_MovingEntity);
			transform.m_Position.m_X = position.m_X;
			transform.m_Position.m_Y = position.m_Y;
		}
	}

	void TemporalAALabSession::OnFrameSubmitted(
		const DemoFrameFeedback& feedback) noexcept
	{
		if (feedback.m_RenderSceneStatus == RenderSceneBuildStatus::Ready &&
			feedback.m_SubmittedFence.IsValid())
		{
			++m_CommittedFrameCount;
		}
		CaptureGpuTiming(feedback);
	}

	void TemporalAALabSession::OnResize(uint32_t width, uint32_t height) noexcept
	{
		LabSessionBase::OnResize(width, height);
		m_ViewportWidth = width;
		m_ViewportHeight = height;
		ResetEvidenceCapture();
	}

	void TemporalAALabSession::ApplyImmediateParameters() noexcept
	{
		const auto& parameters = GetParameters();
		auto& taa = GetMutableViewRenderProfile().m_TemporalAA;
		const bool enabled = parameters.Get(EnabledId, true);
		const float maxHistoryFeedback = parameters.Get(
			MaxHistoryFeedbackId, TemporalAADefaultMaxHistoryFeedback);
		const bool enableCameraInput = parameters.Get(EnableCameraInputId, false);
		const bool animateObject = parameters.Get(AnimateObjectId, true);
		const bool orbitCamera = parameters.Get(OrbitCameraId, false);
		const bool continuousFovZoom = parameters.Get(ContinuousFovZoomId, false);
		const bool evidenceDomainChanged =
			m_EnableCameraInput != enableCameraInput || m_AnimateObject != animateObject ||
			m_OrbitCamera != orbitCamera || m_ContinuousFovZoom != continuousFovZoom;
		taa.m_Enabled = enabled;
		taa.m_MaxHistoryFeedback = maxHistoryFeedback;
		const PostProcessDebugTap selectedTap = static_cast<PostProcessDebugTap>(parameters.Get(
			PreviewTapId, int32_t(PostProcessDebugTap::TemporalHistoryWeight)));
		const bool selectedTapChanged = selectedTap != m_SelectedTap;
		m_SelectedTap = selectedTap;
		m_EnableCameraInput = enableCameraInput;
		m_AnimateObject = animateObject;
		m_OrbitCamera = orbitCamera;
		m_ContinuousFovZoom = continuousFovZoom;
		const uint32_t cutSerial = parameters.Get(CameraCutSerialId, uint32_t(0));
		const bool cameraCutRequested = cutSerial != m_LastCameraCutSerial;
		if (cameraCutRequested)
		{
			m_LastCameraCutSerial = cutSerial;
			GetCamera().RequestTemporalReset();
		}
		if (evidenceDomainChanged || cameraCutRequested)
		{
			ResetEvidenceCapture();
		}
		if (m_IsEntered)
		{
			if (selectedTapChanged)
			{
				ApplySelectedPreviewSelection();
			}
			else
			{
				RequestPreviewRefresh();
			}
		}
	}

	void TemporalAALabSession::RebuildScene() noexcept { BuildScene(); }

	void TemporalAALabSession::OnParametersRestoredForPrepare(LabChangeImpact impact) noexcept
	{
		GGLAB_UNUSED(impact);
		ApplyImmediateParameters();
	}

	void TemporalAALabSession::BuildScene() noexcept
	{
		ResetEvidenceCapture();
		ResetAssetInterests();
		m_AssetPreparation.Reset();
		auto& registry = m_World.GetRegistry();
		registry.clear();
		m_ElapsedSeconds = 0.0f;
		const ModelID alphaModel = GetAssetOwnerScope()
			.LoadModelAsync(std::filesystem::path(AlphaBlendModeTestPath))
			.m_ModelId;
		m_AssetPreparation.TrackModel(alphaModel, AlphaBlendModeTestPath, 0.4f);
		m_AssetPreparation.TrackModel(ProceduralCubeModelID, "ProceduralCube", 0.35f);
		m_AssetPreparation.TrackModel(ProceduralSphereModelID, "ProceduralSphere", 0.25f);
		GetCamera().LookAt(FixtureViewPosition, FixtureViewTarget);
		GetCamera().SetFov(FixtureViewFov);
		GetCamera().Update();
		// Static fixture views whose scene changes with the path frame.
		const auto makeFixturePath = [this](std::string_view id, const char* name,
			const char* purpose)
			{
				return CameraPath{
					.m_Id = std::string(id),
					.m_Name = name,
					.m_Purpose = purpose,
					.m_Version = 1,
					.m_Interpolation = CameraPathInterpolation::Linear,
					.m_NearPlane = GetCamera().GetNear(),
					.m_FarPlane = GetCamera().GetFar(),
					.m_ManualEV100 = GetCamera().GetManualEV100(),
					.m_ExposureCompensationEV = GetCamera().GetExposureCompensationEV(),
					.m_Keys = {
						{ .m_Frame = 0, .m_Position = FixtureViewPosition,
							.m_Target = FixtureViewTarget, .m_VerticalFovDegrees = FixtureViewFov },
						{ .m_Frame = 179, .m_Position = FixtureViewPosition,
							.m_Target = FixtureViewTarget, .m_VerticalFovDegrees = FixtureViewFov },
					},
				};
			};
		const bool pathsRegistered = GetCameraRig().SetCameraPaths({
			// Geometry correspondence stays exact while every surface's shading changes.
			makeFixturePath(LightChangePathId, "Light Change",
				"Static fixture view; the directional light steps to 60% at frame 60 and fades "
				"back over frames 120-179."),
				// Correspondence of a moving rigid object over a static background.
				makeFixturePath(MovingObjectPathId, "Moving Object",
					"Static fixture view; the rigid sphere moves with the path frame, revealing and "
					"covering the background behind it."),
			});
		GGLAB_ASSERT_MSG(pathsRegistered, "The Temporal AA Lab camera paths must be valid.");

		const auto createCube = [this](std::string_view key, const Vector3& position,
			const Vector3& scale, const Color& color, float roughness, float metallic = 0.0f,
			const Color& emissive = Color::Black) noexcept
			{
				return primitive::Cube::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_RenderServices.m_Samplers,
					.m_World = &m_World,
					.m_Transform = components::TransformComponent{
						.m_Position = position, .m_Scale = scale },
					.m_MaterialInstance = MakeMaterial(key, color, roughness, metallic, emissive),
					});
			};
		const entt::entity floor = createCube("gglab.lab.temporal_aa.floor",
			Vector3(0.0f, -1.5f, 7.0f), Vector3(8.0f, 0.15f, 9.0f),
			Color(0.12f, 0.14f, 0.18f, 1.0f), 0.8f);
		const entt::entity thinA = createCube("gglab.lab.temporal_aa.thin_a",
			Vector3(-3.0f, 0.2f, 6.0f), Vector3(0.035f, 1.8f, 1.0f),
			Color(0.9f, 0.2f, 0.08f, 1.0f), 0.45f);
		const entt::entity thinB = createCube("gglab.lab.temporal_aa.thin_b",
			Vector3(-2.2f, 0.2f, 7.5f), Vector3(0.04f, 1.6f, 1.0f),
			Color(0.1f, 0.7f, 0.95f, 1.0f), 0.3f);
		const entt::entity emissive = createCube("gglab.lab.temporal_aa.emissive",
			Vector3(3.3f, -0.2f, 5.2f), Vector3(0.45f, 0.45f, 0.45f),
			Color(0.02f, 0.02f, 0.02f, 1.0f), 0.6f, 0.0f,
			Color(5.0f, 0.5f, 0.05f, 1.0f));
		const entt::entity specular = createCube("gglab.lab.temporal_aa.specular",
			Vector3(2.0f, -0.1f, 8.3f), Vector3(0.7f, 0.7f, 0.7f),
			Color(0.9f, 0.92f, 0.96f, 1.0f), 0.04f, 1.0f);
		const entt::entity background = createCube("gglab.lab.temporal_aa.disocclusion_background",
			Vector3(0.0f, 0.0f, 10.5f), Vector3(3.5f, 2.0f, 0.12f),
			Color(0.15f, 0.65f, 0.3f, 1.0f), 0.7f);
		const entt::entity distantBoard = createCube("gglab.lab.temporal_aa.distant_board",
			Vector3(7.0f, 4.5f, 45.0f), Vector3(3.0f, 1.75f, 0.12f),
			Color(0.08f, 0.72f, 0.95f, 1.0f), 0.35f);
		components::TransformComponent distantReferenceTransform{};
		distantReferenceTransform.m_Position = Vector3(0.0f, 5.5f, 45.0f);
		distantReferenceTransform.m_Scale = Vector3::One * 2.25f;
		const entt::entity distantReference = primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_RenderServices.m_Samplers,
			.m_World = &m_World, .m_Transform = distantReferenceTransform,
			.m_MaterialInstance = MakeMaterial("gglab.lab.temporal_aa.distant_reference",
				Color(0.95f, 0.72f, 0.08f, 1.0f), 0.28f, 0.15f),
			});

		// The world-size pair differs only in placement. It exposes the natural projected-size
		// change with distance without changing shape, material, orientation, or lighting.
		const Vector3 matchedWorldScale(0.055f, 0.8f, 0.12f);
		const Color matchedWorldColor(0.96f, 0.35f, 0.08f, 1.0f);
		const entt::entity matchedWorldNear = createCube(
			"gglab.lab.temporal_aa.matched_world_near", Vector3(-4.8f, 2.8f, 8.0f),
			matchedWorldScale, matchedWorldColor, 0.4f);
		const entt::entity matchedWorldFar = createCube(
			"gglab.lab.temporal_aa.matched_world_far", Vector3(-7.5f, 5.2f, 44.0f),
			matchedWorldScale, matchedWorldColor, 0.4f);

		// Scale the far fixture by the exact center-point camera-space depth ratio. The pair
		// otherwise keeps equivalent shape, material, orientation, lighting, and contrast.
		const Vector3 matchedProjectedNearPosition(4.5f, 2.8f, 8.0f);
		const Vector3 matchedProjectedFarPosition(9.0f, 5.2f, 44.0f);
		const Vector3 matchedProjectedNearScale(0.055f, 0.8f, 0.12f);
		const float matchedProjectedNearDepth =
			(matchedProjectedNearPosition - GetCamera().GetPosition()).Dot(
				GetCamera().GetForward());
		const float matchedProjectedFarDepth =
			(matchedProjectedFarPosition - GetCamera().GetPosition()).Dot(
				GetCamera().GetForward());
		const Vector3 matchedProjectedFarScale = matchedProjectedNearScale *
			(matchedProjectedFarDepth / matchedProjectedNearDepth);
		const Color matchedProjectedColor(0.78f, 0.95f, 0.12f, 1.0f);
		const entt::entity matchedProjectedNear = createCube(
			"gglab.lab.temporal_aa.matched_projected_near", matchedProjectedNearPosition,
			matchedProjectedNearScale, matchedProjectedColor, 0.4f);
		const entt::entity matchedProjectedFar = createCube(
			"gglab.lab.temporal_aa.matched_projected_far", matchedProjectedFarPosition,
			matchedProjectedFarScale, matchedProjectedColor, 0.4f);
		const entt::entity alphaEdge = registry.create();
		components::TransformComponent alphaTransform{};
		alphaTransform.m_Position = Vector3(-4.2f, -0.25f, 6.8f);
		alphaTransform.m_Scale = Vector3::One * 0.75f;
		registry.emplace<components::TransformComponent>(alphaEdge, alphaTransform);
		registry.emplace<components::ModelComponent>(
			alphaEdge, components::ModelComponent{ .m_ModelId = alphaModel });

		components::TransformComponent movingTransform{};
		movingTransform.m_Position = Vector3(0.0f, -0.15f, 7.5f);
		movingTransform.m_Scale = Vector3::One * 1.25f;
		m_MovingEntity = primitive::Sphere::Create({
			.m_AssetManager = m_Services.m_AssetManager,
			.m_SamplerRegistry = m_Services.m_RenderServices.m_Samplers,
			.m_World = &m_World, .m_Transform = movingTransform,
			.m_MaterialInstance = MakeMaterial("gglab.lab.temporal_aa.moving_rigid",
				Color(0.7f, 0.12f, 0.5f, 1.0f), 0.22f),
			});
		const entt::entity fixtures[] = {
			floor, thinA, thinB, emissive, specular, background, distantBoard,
			distantReference, matchedWorldNear, matchedWorldFar, matchedProjectedNear,
			matchedProjectedFar, alphaEdge, m_MovingEntity };
		m_FixtureConfigured = std::ranges::all_of(fixtures, [&registry](entt::entity entity)
			{
				return registry.valid(entity);
			});
		BuildLighting();
		ApplyImmediateParameters();
	}

	void TemporalAALabSession::BuildLighting() noexcept
	{
		auto& registry = m_World.GetRegistry();
		const entt::entity entity = registry.create();
		Vector3 direction(-0.35f, -0.82f, -0.45f);
		direction.Normalize();
		components::TransformComponent transform{};
		transform.m_Rotation = math::RotationFromTo(Vector3::Forward, direction);
		registry.emplace<components::TransformComponent>(entity, transform);
		components::LightComponent light{};
		light.m_Type = LightType::Directional;
		light.m_Color = Color::White;
		light.m_Intensity = LightIntensity;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(entity, light);
		m_LightEntity = entity;
	}

	void TemporalAALabSession::ApplySelectedPreviewSelection() noexcept
	{
		if (auto* registry = m_Services.m_RenderServices.m_Resources)
		{
			registry->SetPostProcessPreviewSelection({ .m_Tap = m_SelectedTap },
				PostProcessPreviewChannel::TemporalAA);
			registry->RequestPostProcessPreview(PostProcessPreviewChannel::TemporalAA);
		}
	}

	void TemporalAALabSession::RequestPreviewRefresh() noexcept
	{
		if (auto* registry = m_Services.m_RenderServices.m_Resources)
		{
			registry->RequestPostProcessPreview(PostProcessPreviewChannel::TemporalAA);
		}
	}

	void TemporalAALabSession::CaptureGpuTiming(const DemoFrameFeedback& feedback) noexcept
	{
		const auto* profilingView = m_Services.m_GpuProfiling;
		const bool enabled = profilingView && profilingView->IsEnabled();
		m_GpuTiming.OnFrameSubmitted(feedback, m_Services.m_RHIContext->GetFrameSlotCount(), enabled);
		if (!enabled || m_GpuTiming.GetSnapshot().m_SampleCount >= TemporalAALabGpuTimingSampleTarget ||
			!m_GpuTiming.Capture(profilingView->GetLatestFrame()))
		{
			return;
		}
		const auto& timing = m_GpuTiming.GetSnapshot();
		const auto& domain = *timing.m_Domain;
		const auto* device = m_Services.m_RenderServices.m_Presentation->GetDevice();
		const std::string_view cameraMode = m_EnableCameraInput ? "input"
			: (m_OrbitCamera ? "pan/rotation" : (m_ContinuousFovZoom ? "continuous-fov" : "static"));
		GGLAB_LOG_INFO(
			"Temporal AA timing window complete: adapter='{}', render={}x{}, display={}x{}, "
			"settings='{}', lab_animate_object={}, lab_camera_mode='{}', jitter_phases={}, "
			"warmup_frames={}, domain_first_frame={}, eligible_first_frame={}, "
			"sample_first_frame={}, sample_last_frame={}, gpu_frame={}, samples={}, "
			"average_ms={:.6f}, min_ms={:.6f}, max_ms={:.6f}.",
			device ? device->GetAdapterCompatibilityIdentity() : "unavailable",
			domain.m_Resolution.m_Render.m_Width, domain.m_Resolution.m_Render.m_Height,
			domain.m_Resolution.m_Display.m_Width, domain.m_Resolution.m_Display.m_Height,
			FormatTemporalAALabSettings(domain.m_Settings), m_AnimateObject, cameraMode,
			GetTemporalAAJitterSequenceLength(domain.m_Settings.m_ResolutionPreset),
			timing.m_WarmupFrameCount, timing.m_FirstDomainFrameSerial,
			timing.m_FirstEligibleFrameSerial, timing.m_FirstSampleFrameSerial,
			timing.m_LastSampleFrameSerial, timing.m_LastGpuFrameIndex, timing.m_SampleCount,
			timing.m_SumMilliseconds / static_cast<double>(timing.m_SampleCount),
			timing.m_MinMilliseconds, timing.m_MaxMilliseconds);
	}

	void TemporalAALabSession::ResetEvidenceCapture() noexcept
	{
		m_CommittedFrameCount = 0;
		m_GpuTiming.Reset();
	}

	void TemporalAALabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		BuildFrameDiagnostics(diagnostics, {});
	}

	void TemporalAALabSession::BuildFrameDiagnostics(LabDiagnosticsSnapshot& diagnostics,
		const LabDiagnosticsFrameContext& context) const noexcept
	{
		const auto history =
			m_Services.m_RenderServices.m_Temporal->GetTemporalHistoryDiagnostics();
		const auto* device = m_Services.m_RenderServices.m_Presentation->GetDevice();
		const auto& camera = GetCamera();
		const auto& taa = GetViewRenderProfile().m_TemporalAA;
		const float ceilingSaturationAge = ResolveTemporalAAFeedbackSaturationAge(
			TemporalAAMaxHistoryFeedbackCeiling);
		const float packedCeiling = UnpackTemporalAAUnitRangePair(
			PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
				TemporalAAMaxHistoryFeedbackCeiling, 0.0f))[0];
		const float packedCeilingSaturationAge =
			ResolveTemporalAAFeedbackSaturationAge(packedCeiling);
		const bool coupledBoundValid = ceilingSaturationAge <= TemporalHistoryMaxAccumulation &&
			packedCeilingSaturationAge <= TemporalHistoryMaxAccumulation && packedCeiling < 1.0f &&
			ResolveTemporalAAMaxHistorySamples(packedCeiling) < TemporalHistoryMaxAccumulation;
		const auto& timing = m_GpuTiming.GetSnapshot();
		const std::string gpuTiming = timing.m_SampleCount > 0
			? std::format("{:.3f} ms avg [{:.3f}, {:.3f}], {}/{} samples",
				timing.m_SumMilliseconds / static_cast<double>(timing.m_SampleCount),
				timing.m_MinMilliseconds, timing.m_MaxMilliseconds,
				timing.m_SampleCount, TemporalAALabGpuTimingSampleTarget)
			: timing.m_Domain ? std::format("warmup remaining {}/{} submitted frames; 0/{} samples",
				timing.m_WarmupFramesRemaining, timing.m_WarmupFrameCount, TemporalAALabGpuTimingSampleTarget)
			: "waiting for an active submitted TAA frame with profiling";
		diagnostics.m_Title = "Temporal AA Evidence Domain";
		diagnostics.m_Metrics = {
			{ .m_Name = "Adapter/driver domain",
				.m_Value = device ? std::string(device->GetAdapterCompatibilityIdentity())
					: "unavailable" },
			{ .m_Name = "Extent", .m_Value = std::format("{} x {}",
				m_ViewportWidth, m_ViewportHeight) },
			{ .m_Name = "Camera transform", .m_Value = std::format(
				"pos ({:.3f}, {:.3f}, {:.3f}), yaw {:.5f}, pitch {:.5f}",
				camera.GetPosition().m_X, camera.GetPosition().m_Y, camera.GetPosition().m_Z,
				camera.GetYaw(), camera.GetPitch()) },
			{ .m_Name = "Camera optics", .m_Value = std::format(
				"FOV {:.3f} deg, exposure {:.3f} EV", camera.GetFov(),
				camera.GetExposureCompensationEV()) },
			{ .m_Name = "Frozen ceiling", .m_Value = std::format(
				"{:.6f} -> age {:.0f}; packed {:.6f} -> age {:.0f}",
				TemporalAAMaxHistoryFeedbackCeiling, ceilingSaturationAge,
				packedCeiling, packedCeilingSaturationAge) },
			{ .m_Name = "Evidence frames", .m_Value = std::format(
				"{} committed after reset; last jitter {}", m_CommittedFrameCount,
				history.m_LastCommitted.m_JitterIndex) },
			{ .m_Name = "PostProcess.TemporalAA (submitted domain)", .m_Value = gpuTiming },
			{ .m_Name = "GPU timing settings", .m_Value = timing.m_Domain
				? FormatTemporalAALabSettings(timing.m_Domain->m_Settings) : "unavailable" },
			{ .m_Name = "GPU timing frame range", .m_Value = std::format(
				"domain from {}; first eligible {}; samples {}-{}",
				timing.m_FirstDomainFrameSerial, timing.m_FirstEligibleFrameSerial,
				timing.m_FirstSampleFrameSerial, timing.m_LastSampleFrameSerial) },
			{ .m_Name = "History", .m_Value = history.m_HistoryValid ? "valid" : "invalid" },
			{ .m_Name = "Compatibility generation",
				.m_Value = std::format("{}", history.m_AllocationGeneration) },
			{ .m_Name = "History pressure",
				.m_Value = std::format("{} active / {} pending bytes", history.m_ActiveBytes,
					history.m_PendingRetirementBytes) },
			{ .m_Name = "Camera mode", .m_Value = m_OrbitCamera ? "pan/rotation"
				: (m_ContinuousFovZoom ? "continuous FOV zoom" : "static") },
		};
		diagnostics.m_Checks = {
			{ .m_Name = "Deterministic fixture",
				.m_Status = m_FixtureConfigured ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Thin and alpha-test edges, emissive/specular highlights, rigid motion, foreground disocclusion, background sky, curved/straight references, and controlled matched-world-size and matched-projected-footprint pairs are present." },
			{ .m_Name = "Coupled accumulation bound",
				.m_Status = coupledBoundValid ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Failed,
				.m_Detail = "The selected feedback ceiling must saturate both accumulation models below the stored accumulation bound and its UNORM16 representation must remain below one." },
			{ .m_Name = "Submitted-domain GPU timing capture",
				.m_Status = timing.m_SampleCount >= TemporalAALabGpuTimingSampleTarget
					? LabDiagnosticCheckStatus::Passed : LabDiagnosticCheckStatus::Pending,
				.m_Detail = "The shown submitted domain needs 120 distinct completed profiles after two effective jitter cycles (16 Native or 36 Quality frames), at least the RHI frame-slot count. Effective settings, extents, view/session/cut identity and submission gaps reset it; older or unbound profiles are excluded." },
			{ .m_Name = "Sampling-footprint review",
				.m_Status = LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Inspect named edge/disocclusion ROIs for the selected history filter: Bilinear blends 2x2 RGB texels; CatmullRomClamped (default) uses five bilinear fetches over a 4x4 kernel with the corners omitted, then clamps to the central 2x2 texel range. Accumulation is point sampled (1 texel); depth acceptance searches a 3x3 neighborhood. A pass requires no material mismatch artifact." },
		};
		AppendTemporalAALabSettingsDiagnostics(diagnostics, taa, context, history);
	}

	LabId TemporalAALabSession::GetId() noexcept { return LabId("gglab.lab.temporal_aa"); }

	LabDescriptor TemporalAALabSession::GetDescriptor() noexcept
	{
		return {
			.m_Id = GetId(), .m_DisplayName = "Temporal AA", .m_Category = "Rendering",
			.m_Description = "Exercises temporal stability across near/far geometry, motion, disocclusion, camera cuts, resize/toggle pressure, and diagnostic taps.",
			// Schema 2 prevents old fixed-weight presets from being reinterpreted as a cap.
			.m_Kind = LabKind::Pipeline, .m_SchemaVersion = 2,
		};
	}

	std::unique_ptr<LabSessionBase> TemporalAALabSession::Create(
		const LabSessionCreateInfo& createInfo) noexcept
	{
		return std::make_unique<TemporalAALabSession>(createInfo);
	}
}
