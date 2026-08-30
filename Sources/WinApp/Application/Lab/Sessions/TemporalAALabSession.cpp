#include "Application/Lab/Sessions/TemporalAALabSession.h"
#include "AppRuntimeLog.h"

#include "Core/Math/MathFunctions.h"
#include "Core/Math/Quaternion.h"
#include "Diagnostics/Snapshots/LabSnapshot.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Geometry.h"
#include "Graphics/Pipeline/TemporalHistoryManager.h"
#include "Graphics/Profiling/GpuProfiler.h"
#include "Graphics/Renderer.h"
#include "Graphics/RenderPipeline/RenderPipelineForwardPBR.h"
#include "Graphics/Resource/RenderResourceRegistry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <ranges>

namespace gglab
{
	namespace
	{
		constexpr std::string_view AlphaBlendModeTestPath =
			"Assets/Models/AlphaBlendModeTest/AlphaBlendModeTest.gltf";
		constexpr uint32_t TemporalAAEvidenceJitterCycleCount = 2;
		constexpr uint32_t TemporalAAEvidenceWarmupFrameCount =
			temporal::JitterSampleCount * TemporalAAEvidenceJitterCycleCount;
		constexpr uint32_t TemporalAAGpuTimingSampleTarget = 120;

		const LabParameterId EnabledId("temporal_aa.enabled");
		const LabParameterId PreviewTapId("temporal_aa.preview_tap");
		const LabParameterId EnableCameraInputId("temporal_aa.camera.enable_input");
		const LabParameterId OrbitCameraId("temporal_aa.camera.orbit");
		const LabParameterId ContinuousFovZoomId("temporal_aa.camera.continuous_fov_zoom");
		const LabParameterId CameraCutSerialId("temporal_aa.camera.cut_serial");
		const LabParameterId AnimateObjectId("temporal_aa.object.animate");
		const LabParameterId MaxHistoryFeedbackId("temporal_aa.max_history_feedback");

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
		LabSessionBase(GetDescriptor(), createInfo, std::make_unique<RenderPipelineForwardPBR>()),
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
				{.m_Value = int32_t(PostProcessDebugTap::TemporalHistoryColor), .m_Name = "History Color"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalReprojectionUV), .m_Name = "Reprojection UV"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalRejection), .m_Name = "Rejection"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalHistoryWeight), .m_Name = "History Weight"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalHistoryAge), .m_Name = "History Age"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalMotionDirection), .m_Name = "Motion Direction"},
				{.m_Value = int32_t(PostProcessDebugTap::TemporalMotionMagnitude), .m_Name = "Motion Magnitude"},
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
		if (auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler())
		{
			m_GpuProfilerWasEnabled = gpuProfiler->IsEnabled();
			gpuProfiler->SetEnabled(true);
		}
		ResetEvidenceCapture();
		auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry();
		m_PreviousPreviewSelection = registry->GetPostProcessPreviewSelection();
		m_IsEntered = true;
		ApplySelectedPreviewSelection();
	}

	void TemporalAALabSession::OnExit() noexcept
	{
		m_IsEntered = false;
		if (auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler())
		{
			gpuProfiler->SetEnabled(m_GpuProfilerWasEnabled);
		}
		if (auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry())
		{
			registry->SetPostProcessPreviewSelection(m_PreviousPreviewSelection);
			registry->RequestPostProcessPreview();
		}
	}

	void TemporalAALabSession::Update(float deltaTime) noexcept
	{
		m_ElapsedSeconds += deltaTime;
		auto& registry = m_World.GetRegistry();
		if (m_AnimateObject && registry.valid(m_MovingEntity))
		{
			auto& transform = registry.get<components::TransformComponent>(m_MovingEntity);
			transform.m_Position.m_X = std::sin(m_ElapsedSeconds * 1.35f) * 3.2f;
			transform.m_Position.m_Y = -0.15f + std::sin(m_ElapsedSeconds * 0.7f) * 0.35f;
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
		CaptureGpuTiming();
	}

	void TemporalAALabSession::OnFrameSubmitted(
		const DemoFrameFeedback& feedback) noexcept
	{
		if (feedback.m_RenderSceneStatus == RenderSceneBuildStatus::Ready &&
			feedback.m_SubmittedFence.IsValid())
		{
			++m_CommittedFrameCount;
		}
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
		const bool evidenceDomainChanged = taa.m_Enabled != enabled ||
			taa.m_MaxHistoryFeedback != maxHistoryFeedback ||
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
		GetCamera().LookAt(Vector3(0.0f, 2.2f, -10.0f), Vector3(0.0f, 0.0f, 6.5f));
		GetCamera().SetFov(52.0f);
		GetCamera().Update();

		const auto createCube = [this](std::string_view key, const Vector3& position,
			const Vector3& scale, const Color& color, float roughness, float metallic = 0.0f,
			const Color& emissive = Color::Black) noexcept
			{
				return primitive::Cube::Create({
					.m_AssetManager = m_Services.m_AssetManager,
					.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
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
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
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
			.m_SamplerRegistry = m_Services.m_Renderer->GetSamplerRegistry(),
			.m_World = &m_World, .m_Transform = movingTransform,
			.m_MaterialInstance = MakeMaterial("gglab.lab.temporal_aa.moving_rigid",
				Color(0.7f, 0.12f, 0.5f, 1.0f), 0.22f),
			});
		const entt::entity fixtures[] = {
			floor, thinA, thinB, emissive, specular, background, distantBoard,
			distantReference, matchedWorldNear, matchedWorldFar, matchedProjectedNear,
			matchedProjectedFar, alphaEdge, m_MovingEntity };
		m_FixtureConfigured = std::ranges::all_of(fixtures, [&registry](entt::entity entity)
			{ return registry.valid(entity); });
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
		light.m_Intensity = 3.5f;
		light.m_Range = 1000.0f;
		registry.emplace<components::LightComponent>(entity, light);
	}

	void TemporalAALabSession::ApplySelectedPreviewSelection() noexcept
	{
		if (auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry())
		{
			registry->SetPostProcessPreviewSelection({ .m_Tap = m_SelectedTap });
			registry->RequestPostProcessPreview();
		}
	}

	void TemporalAALabSession::RequestPreviewRefresh() noexcept
	{
		if (auto* registry = m_Services.m_Renderer->GetRenderResourceRegistry())
		{
			registry->RequestPostProcessPreview();
		}
	}

	void TemporalAALabSession::CaptureGpuTiming() noexcept
	{
		if (m_GpuTimingSampleCount >= TemporalAAGpuTimingSampleTarget)
		{
			return;
		}
		auto* gpuProfiler = m_Services.m_Renderer->GetGpuProfiler();
		if (!gpuProfiler || !gpuProfiler->IsEnabled())
		{
			return;
		}
		const GpuProfileFrameSnapshot frame = gpuProfiler->GetLatestFrame();
		if (!frame.IsValid() || frame.m_FrameIndex == m_LastGpuProfileFrame)
		{
			return;
		}
		m_LastGpuProfileFrame = frame.m_FrameIndex;
		if (m_GpuTimingWarmupFrames > 0)
		{
			--m_GpuTimingWarmupFrames;
			return;
		}

		double temporalAAMilliseconds = 0.0;
		bool hasTemporalAASample = false;
		for (const auto& sample : frame.m_Samples)
		{
			if (sample.m_Name == "PostProcess.TemporalAA")
			{
				temporalAAMilliseconds += sample.m_Milliseconds;
				hasTemporalAASample = true;
			}
		}
		if (!hasTemporalAASample)
		{
			return;
		}

		if (m_GpuTimingSampleCount == 0)
		{
			m_GpuTimingMinMilliseconds = temporalAAMilliseconds;
			m_GpuTimingMaxMilliseconds = temporalAAMilliseconds;
		}
		else
		{
			m_GpuTimingMinMilliseconds =
				std::min(m_GpuTimingMinMilliseconds, temporalAAMilliseconds);
			m_GpuTimingMaxMilliseconds =
				std::max(m_GpuTimingMaxMilliseconds, temporalAAMilliseconds);
		}
		m_GpuTimingSumMilliseconds += temporalAAMilliseconds;
		++m_GpuTimingSampleCount;
		if (m_GpuTimingSampleCount == TemporalAAGpuTimingSampleTarget)
		{
			const auto* device = m_Services.m_Renderer->GetDevice();
			const std::string_view adapterIdentity = device
				? device->GetAdapterCompatibilityIdentity() : "unavailable";
			const auto& taa = GetViewRenderProfile().m_TemporalAA;
			const std::string_view cameraMode = m_OrbitCamera ? "pan/rotation"
				: (m_ContinuousFovZoom ? "continuous-fov" : "static");
			GGLAB_LOG_INFO(
				"Temporal AA A4 timing window complete: adapter='{}', extent={}x{}, "
				"feedback={:.6f}, animate_object={}, camera_mode='{}', samples={}, "
				"average_ms={:.6f}, min_ms={:.6f}, max_ms={:.6f}.",
				adapterIdentity, m_ViewportWidth, m_ViewportHeight,
				taa.m_MaxHistoryFeedback, m_AnimateObject, cameraMode,
				m_GpuTimingSampleCount,
				m_GpuTimingSumMilliseconds / static_cast<double>(m_GpuTimingSampleCount),
				m_GpuTimingMinMilliseconds, m_GpuTimingMaxMilliseconds);
		}
	}

	void TemporalAALabSession::ResetEvidenceCapture() noexcept
	{
		m_LastGpuProfileFrame = 0;
		m_CommittedFrameCount = 0;
		m_GpuTimingSampleCount = 0;
		m_GpuTimingSumMilliseconds = 0.0;
		m_GpuTimingMinMilliseconds = 0.0;
		m_GpuTimingMaxMilliseconds = 0.0;
		ArmGpuTimingCaptureWarmup();
	}

	void TemporalAALabSession::ArmGpuTimingCaptureWarmup() noexcept
	{
		const auto* rhiContext = m_Services.m_Renderer
			? m_Services.m_Renderer->GetRHIContext()
			: nullptr;
		m_GpuTimingWarmupFrames = std::max(TemporalAAEvidenceWarmupFrameCount,
			rhiContext ? rhiContext->GetFrameSlotCount() : 3u);
	}

	void TemporalAALabSession::BuildDiagnostics(LabDiagnosticsSnapshot& diagnostics) const noexcept
	{
		const auto history =
			m_Services.m_Renderer->GetTemporalHistoryManager()->GetDiagnostics();
		const auto* device = m_Services.m_Renderer->GetDevice();
		const auto& camera = GetCamera();
		const auto& taa = GetViewRenderProfile().m_TemporalAA;
		const float saturationAge =
			ResolveTemporalAAFeedbackSaturationAge(taa.m_MaxHistoryFeedback);
		const float ceilingSaturationAge = ResolveTemporalAAFeedbackSaturationAge(
			TemporalAAMaxHistoryFeedbackCeiling);
		const float packedCeiling = UnpackTemporalAAUnitRangePair(
			PackTemporalAAMaxHistoryFeedbackAndClampExpansion(
				TemporalAAMaxHistoryFeedbackCeiling, 0.0f))[0];
		const float packedCeilingSaturationAge =
			ResolveTemporalAAFeedbackSaturationAge(packedCeiling);
		const bool coupledBoundValid = ceilingSaturationAge <= TemporalHistoryMaxAge &&
			packedCeilingSaturationAge <= TemporalHistoryMaxAge && packedCeiling < 1.0f;
		const std::string gpuTiming = m_GpuTimingSampleCount > 0
			? std::format("{:.3f} ms avg [{:.3f}, {:.3f}], {}/{} samples",
				m_GpuTimingSumMilliseconds / static_cast<double>(m_GpuTimingSampleCount),
				m_GpuTimingMinMilliseconds, m_GpuTimingMaxMilliseconds,
				m_GpuTimingSampleCount, TemporalAAGpuTimingSampleTarget)
			: std::format("warming up, 0/{} samples", TemporalAAGpuTimingSampleTarget);
		diagnostics.m_Title = "Temporal AA A4 Evidence Domain";
		diagnostics.m_Metrics = {
			{.m_Name = "Adapter/driver domain",
				.m_Value = device ? std::string(device->GetAdapterCompatibilityIdentity())
					: "unavailable"},
			{.m_Name = "Extent", .m_Value = std::format("{} x {}",
				m_ViewportWidth, m_ViewportHeight)},
			{.m_Name = "Camera transform", .m_Value = std::format(
				"pos ({:.3f}, {:.3f}, {:.3f}), yaw {:.5f}, pitch {:.5f}",
				camera.GetPosition().m_X, camera.GetPosition().m_Y, camera.GetPosition().m_Z,
				camera.GetYaw(), camera.GetPitch())},
			{.m_Name = "Camera optics", .m_Value = std::format(
				"FOV {:.3f} deg, exposure {:.3f} EV", camera.GetFov(),
				camera.GetExposureCompensationEV())},
			{.m_Name = "TAA settings", .m_Value = std::format(
				"feedback {:.6f}, velocity {:.6f}, luminance {:.6f}, clamp {:.6f}",
				taa.m_MaxHistoryFeedback, taa.m_VelocityWeightScale,
				taa.m_LuminanceWeightScale, taa.m_NeighborhoodClampExpansion)},
			{.m_Name = "Age bound", .m_Value = std::format(
				"saturation {:.0f}, max {:.0f}", saturationAge, TemporalHistoryMaxAge)},
			{.m_Name = "Frozen ceiling", .m_Value = std::format(
				"{:.6f} -> age {:.0f}; packed {:.6f} -> age {:.0f}",
				TemporalAAMaxHistoryFeedbackCeiling, ceilingSaturationAge,
				packedCeiling, packedCeilingSaturationAge)},
			{.m_Name = "Evidence frames", .m_Value = std::format(
				"{} committed after reset; last jitter {}", m_CommittedFrameCount,
				history.m_LastCommitted.m_JitterIndex)},
			{.m_Name = "PostProcess.TemporalAA", .m_Value = gpuTiming},
			{.m_Name = "History", .m_Value = history.m_HistoryValid ? "valid" : "invalid"},
			{.m_Name = "Compatibility generation",
				.m_Value = std::format("{}", history.m_AllocationGeneration)},
			{.m_Name = "History pressure",
				.m_Value = std::format("{} active / {} pending bytes", history.m_ActiveBytes,
					history.m_PendingRetirementBytes)},
			{.m_Name = "Camera mode", .m_Value = m_OrbitCamera ? "pan/rotation"
				: (m_ContinuousFovZoom ? "continuous FOV zoom" : "static")},
		};
		diagnostics.m_Checks = {
			{.m_Name = "Deterministic fixture",
				.m_Status = m_FixtureConfigured ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Thin and alpha-test edges, emissive/specular highlights, rigid motion, foreground disocclusion, background sky, curved/straight references, and controlled matched-world-size and matched-projected-footprint pairs are present."},
			{.m_Name = "Temporal history allocation",
				.m_Status = GetViewRenderProfile().m_TemporalAA.m_Enabled && history.m_HasActiveHistory
					? LabDiagnosticCheckStatus::Passed : LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Enable TAA and allow one submitted frame to allocate fresh history."},
			{.m_Name = "Coupled accumulation bound",
				.m_Status = coupledBoundValid ? LabDiagnosticCheckStatus::Passed
					: LabDiagnosticCheckStatus::Failed,
				.m_Detail = "The selected feedback ceiling must saturate no later than MaxHistoryAge and its UNORM16 representation must remain below one."},
			{.m_Name = "GPU timing capture",
				.m_Status = m_GpuTimingSampleCount >= TemporalAAGpuTimingSampleTarget
					? LabDiagnosticCheckStatus::Passed : LabDiagnosticCheckStatus::Pending,
				.m_Detail = "The fixed 120-frame window starts after two complete 8-sample jitter cycles and resets when the evidence domain changes."},
			{.m_Name = "Sampling-footprint review",
				.m_Status = LabDiagnosticCheckStatus::Pending,
				.m_Detail = "Inspect named edge/disocclusion ROIs: RGB is bilinear (4 texels), age is point sampled (1 texel), and depth acceptance searches a 3x3 neighborhood. A pass requires no material mismatch artifact."},
		};
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
