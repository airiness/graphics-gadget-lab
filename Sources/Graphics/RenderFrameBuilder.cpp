#include "Core/Precompiled.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/Asset/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/CameraRig.h"
#include "Graphics/Renderer.h"
#include "Core/Math/Culling.h"
#include "Core/Profiling/CpuProfiler.h"

namespace gglab
{
	namespace
	{
		struct FrustumList
		{
			std::array<math::Frustum, 2> m_Frustums{};
			uint32_t m_Count = 0;

			void Add(const math::Frustum& frustum) noexcept
			{
				if (m_Count < m_Frustums.size())
				{
					m_Frustums[m_Count++] = frustum;
				}
			}

			std::span<const math::Frustum> AsSpan() const noexcept
			{
				return std::span<const math::Frustum>(m_Frustums.data(), m_Count);
			}
		};

		[[nodiscard]] bool IsValidBuiltView(
			std::span<const RenderView> renderViews,
			RenderViewID viewId) noexcept
		{
			const size_t index = utils::ToIndex(viewId);
			return index < renderViews.size() &&
				renderViews[index].m_ViewId == viewId &&
				renderViews[index].m_IsValid;
		}

		[[nodiscard]] RenderViewVisibilityMode GetVisibilityModeForView(
			const CameraRig& cameraRig,
			RenderViewID viewId) noexcept
		{
			if (viewId == RenderViewID::Main)
			{
				return RenderViewVisibilityMode::Self;
			}

			const CameraRig::CameraSlot* slot = cameraRig.FindRenderViewSlot(viewId);
			return slot ? slot->m_VisibilityMode : RenderViewVisibilityMode::None;
		}

		[[nodiscard]] FrustumList BuildVisibilityFrustums(
			std::span<const RenderView> renderViews,
			RenderViewID viewId,
			RenderViewVisibilityMode mode,
			const math::Frustum& mainFrustum,
			bool hasMainFrustum) noexcept
		{
			FrustumList list{};
			const bool hasSelfFrustum = IsValidBuiltView(renderViews, viewId);
			const math::Frustum selfFrustum = hasSelfFrustum ?
				math::CreateFrustumFromViewProjection(renderViews[utils::ToIndex(viewId)].m_ViewProj) :
				math::Frustum{};

			switch (mode)
			{
			case RenderViewVisibilityMode::Self:
				if (hasSelfFrustum)
				{
					list.Add(selfFrustum);
				}
				break;
			case RenderViewVisibilityMode::MainCamera:
				if (hasMainFrustum)
				{
					list.Add(mainFrustum);
				}
				break;
			case RenderViewVisibilityMode::IntersectionWithMainCamera:
				if (hasSelfFrustum)
				{
					list.Add(selfFrustum);
				}
				if (hasMainFrustum && viewId != RenderViewID::Main)
				{
					list.Add(mainFrustum);
				}
				break;
			case RenderViewVisibilityMode::None:
			default:
				break;
			}

			return list;
		}

		void FillDebugDrawCullContext(
			DebugDrawCullContext& context,
			std::span<const RenderView> renderViews,
			RenderViewID displayViewId,
			RenderViewVisibilityMode displayVisibilityMode,
			const math::Frustum& mainFrustum,
			bool hasMainFrustum) noexcept
		{
			context = {};
			context.m_MainViewFrustum = mainFrustum;
			context.m_HasMainViewFrustum = hasMainFrustum;

			const FrustumList defaults = BuildVisibilityFrustums(
				renderViews,
				displayViewId,
				displayVisibilityMode,
				mainFrustum,
				hasMainFrustum);
			context.m_DefaultFrustumCount = defaults.m_Count;
			for (uint32_t index = 0; index < defaults.m_Count; ++index)
			{
				context.m_DefaultFrustums[index] = defaults.m_Frustums[index];
			}
		}
	}

	RenderFrameContext RenderFrameBuilder::BuildResult::MakeRenderFrameContext() noexcept
	{
		return RenderFrameContext{
			.m_RenderViews = std::span<RenderView>(m_RenderViews),
			.m_ViewRenderSettings = std::span<const ResolvedViewRenderSettings>(m_ViewRenderSettings),
			.m_DisplayViewId = m_DisplayViewId,
			.m_RenderScene = m_RenderScene,
			.m_RenderQueues = std::span<const RenderQueue>(m_RenderQueues),
			.m_DebugDrawFrame = m_DebugDrawFrame,
			.m_DirectionalShadowSettings = m_WorldData.GetMainDirectionalShadowSettings(),
			.m_ShadowVisualizationSettings = m_ShadowVisualizationSettings,
			.m_BackBufferIndex = m_BackBufferIndex,
			.m_UploadFencePoint = m_UploadFencePoint,
			.m_SceneGpuAllocations = &m_SceneGpuAllocations,
			.m_RenderSceneStatus = m_RenderSceneStatus,
		};
	}

	RenderFrameBuilder::BuildResult RenderFrameBuilder::Build(const BuildInfo& info) noexcept
	{
		BuildResult result{};
		result.m_BackBufferIndex = info.m_BackBufferIndex;
		result.m_ShadowVisualizationSettings = &info.m_ShadowVisualizationSettings;
		result.m_WorldData = m_WorldExtractor.Extract(info.m_World);

		result.m_RenderViews.resize(utils::ToIndex(RenderViewID::Count));
		for (size_t index = 0; index < result.m_RenderViews.size(); ++index)
		{
			result.m_RenderViews[index].m_ViewId = static_cast<RenderViewID>(index);
			result.m_RenderViews[index].m_IsValid = false;
		}

		const Camera& mainCamera = info.m_CameraRig.GetMainCamera();
		auto& mainViewSettings = result.m_ViewRenderSettings[utils::ToIndex(RenderViewID::Main)];
		mainViewSettings = ResolveViewRenderSettings(info.m_ViewRenderProfile, mainCamera);
		const RenderViewBuildInfo<RenderViewID::Main> mainViewBuildInfo{
			.m_Camera = mainCamera,
			.m_RenderSettings = mainViewSettings,
			.m_Width = info.m_WindowWidth,
			.m_Height = info.m_WindowHeight,
			.m_Name = StringID("MainView"),
		};
		result.m_RenderViews[utils::ToIndex(RenderViewID::Main)] =
			m_ViewBuilder.Build<RenderViewID::Main>(mainViewBuildInfo);

		for (size_t cameraIndex = 0; cameraIndex < info.m_CameraRig.GetCameraCount(); ++cameraIndex)
		{
			const auto* slot = info.m_CameraRig.GetCameraSlot(cameraIndex);
			if (!slot ||
				!slot->m_IsDebug ||
				!slot->m_EnableRenderView ||
				!IsDebugCameraRenderViewID(slot->m_RenderViewId) ||
				!slot->m_Camera)
			{
				continue;
			}

			const RenderViewID viewId = slot->m_RenderViewId;
			auto& viewSettings = result.m_ViewRenderSettings[utils::ToIndex(viewId)];
			viewSettings = ResolveViewRenderSettings(info.m_ViewRenderProfile, *slot->m_Camera);
			result.m_RenderViews[utils::ToIndex(viewId)] =
				m_ViewBuilder.BuildDebugCameraView(
					viewId,
					*slot->m_Camera,
					viewSettings,
					info.m_WindowWidth,
					info.m_WindowHeight,
					StringID(std::string_view(slot->m_Name)));
		}

		const auto& shadowSettings = result.m_WorldData.GetMainDirectionalShadowSettings();
		const RenderViewBuildInfo<RenderViewID::DirectionalShadow> shadowViewBuildInfo{
			.m_MainView = result.m_RenderViews[utils::ToIndex(RenderViewID::Main)],
			.m_LightDirection = result.m_WorldData.m_MainDirectionalLight.m_Direction,
			.m_ShadowMapSize = shadowSettings.m_ShadowMapSize,
			.m_MaxShadowDistance = shadowSettings.m_MaxShadowDistance,
			.m_CasterExtrusionDistance = shadowSettings.m_CasterExtrusionDistance,
			.m_OrthoPadding = shadowSettings.m_OrthoPadding,
			.m_DepthPadding = shadowSettings.m_DepthPadding,
			.m_Name = StringID("DirectionalShadowView"),
		};
		result.m_RenderViews[utils::ToIndex(RenderViewID::DirectionalShadow)] =
			m_ViewBuilder.Build<RenderViewID::DirectionalShadow>(shadowViewBuildInfo);

		result.m_DisplayViewId = info.m_CameraRig.GetDisplayViewId();
		if (!IsValidBuiltView(result.m_RenderViews, result.m_DisplayViewId))
		{
			result.m_DisplayViewId = RenderViewID::Main;
		}

		const bool hasMainFrustum = IsValidBuiltView(result.m_RenderViews, RenderViewID::Main);
		const math::Frustum mainFrustum = hasMainFrustum ?
			math::CreateFrustumFromViewProjection(
				result.m_RenderViews[utils::ToIndex(RenderViewID::Main)].m_ViewProj) :
			math::Frustum{};
		const RenderViewVisibilityMode displayVisibilityMode = GetVisibilityModeForView(
			info.m_CameraRig,
			result.m_DisplayViewId);
		FillDebugDrawCullContext(
			result.m_DebugDrawCullContext,
			result.m_RenderViews,
			result.m_DisplayViewId,
			displayVisibilityMode,
			mainFrustum,
			hasMainFrustum);

		const RenderSceneBuilder::BuildInfo sceneBuildInfo{
			.m_World = info.m_World,
			.m_AssetManager = info.m_AssetManager,
			.m_SamplerRegistry = *info.m_Renderer.GetSamplerRegistry(),
			.m_TransferManager = *info.m_Renderer.GetTransferManager(),
			.m_RenderResourceRegistry = *info.m_Renderer.GetRenderResourceRegistry(),
			.m_EnvironmentLightingSystem = *info.m_Renderer.GetEnvironmentLightingSystem(),
			.m_RenderViews = std::span<RenderView>(result.m_RenderViews),
			.m_SceneCB = *info.m_Renderer.GetSceneConstantBuffer(),
			.m_ObjectsSB = *info.m_Renderer.GetObjectStructuredBuffer(),
			.m_MaterialsSB = *info.m_Renderer.GetMaterialStructuredBuffer(),
			.m_LightsSB = *info.m_Renderer.GetLightStructuredBuffer(),
			.m_ObjectTable = *info.m_Renderer.GetObjectStructuredBufferTable(),
			.m_MaterialTable = *info.m_Renderer.GetMaterialStructuredBufferTable(),
			.m_LightTable = *info.m_Renderer.GetLightStructuredBufferTable(),
			.m_DirectionalShadowLightKey = shadowSettings.m_Enable ?
				result.m_WorldData.m_MainDirectionalLight.m_EntityKey : std::nullopt,
			.m_ViewsSB = *info.m_Renderer.GetViewStructuredBuffer(),
			.m_CurrentBackBufferIndex = info.m_BackBufferIndex,
		};
		RenderSceneBuilder::BuildResult sceneBuildResult;
		{
			GGLAB_CPU_PROFILE_SCOPE("RenderSceneBuilder");
			sceneBuildResult = m_SceneBuilder.Build(sceneBuildInfo);
		}
		result.m_RenderScene = std::move(sceneBuildResult.m_RenderScene);
		result.m_SceneGpuAllocations = sceneBuildResult.m_GpuAllocations;
		result.m_UploadFencePoint = sceneBuildResult.m_UploadFencePoint;
		result.m_RenderSceneStatus = sceneBuildResult.m_Status;

		for (const RenderView& renderView : result.m_RenderViews)
		{
			if (!renderView.m_IsValid)
			{
				continue;
			}

			auto& renderQueue =
				result.m_RenderQueues[utils::ToIndex(renderView.m_ViewId)];
			renderQueue.m_ViewId = renderView.m_ViewId;

			if (result.m_RenderSceneStatus != RenderSceneBuildStatus::Ready)
			{
				continue;
			}

			const FrustumList queueCullFrustums =
				renderView.m_ViewId == RenderViewID::DirectionalShadow ?
					FrustumList{} :
					BuildVisibilityFrustums(
						result.m_RenderViews,
						renderView.m_ViewId,
						GetVisibilityModeForView(info.m_CameraRig, renderView.m_ViewId),
						mainFrustum,
						hasMainFrustum);

			const RenderQueueBuilder::BuildInfo queueBuildInfo{
				.m_AssetManager = info.m_AssetManager,
				.m_RenderScene = result.m_RenderScene,
				.m_RenderView = renderView,
				.m_CullingFrustums = queueCullFrustums.AsSpan(),
			};
			renderQueue = m_QueueBuilder.Build(queueBuildInfo);
		}

		return result;
	}
}
