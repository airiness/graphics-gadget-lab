#include "Core/Precompiled.h"
#include "Graphics/RenderFrameBuilder.h"
#include "Graphics/AssetManager.h"
#include "Graphics/Camera.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	RenderFrameContext RenderFrameBuilder::BuildResult::MakeRenderFrameContext() noexcept
	{
		return RenderFrameContext{
			.m_RenderViews = std::span<RenderView>(m_RenderViews),
			.m_RenderScene = m_RenderScene,
			.m_RenderQueues = std::span<const RenderQueue>(m_RenderQueues),
			.m_DirectionalShadowSettings = m_WorldData.GetMainDirectionalShadowSettings(),
			.m_ShadowVisualizationSettings = m_ShadowVisualizationSettings,
			.m_BackBufferIndex = m_BackBufferIndex,
			.m_UploadFencePoint = m_UploadFencePoint
		};
	}

	RenderFrameBuilder::BuildResult RenderFrameBuilder::Build(const BuildInfo& info) noexcept
	{
		BuildResult result{};
		result.m_BackBufferIndex = info.m_BackBufferIndex;
		result.m_ShadowVisualizationSettings = &info.m_ShadowVisualizationSettings;
		result.m_WorldData = m_WorldExtractor.Extract(info.m_World);

		result.m_RenderViews.resize(utils::ToIndex(RenderViewID::Count));

		const RenderViewBuildInfo<RenderViewID::Main> mainViewBuildInfo{
			.m_Camera = info.m_Camera,
			.m_Width = info.m_WindowWidth,
			.m_Height = info.m_WindowHeight,
			.m_Name = StringID("MainView"),
		};
		result.m_RenderViews[utils::ToIndex(RenderViewID::Main)] =
			m_ViewBuilder.Build<RenderViewID::Main>(mainViewBuildInfo);

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

		const RenderSceneBuilder::BuildInfo sceneBuildInfo{
			.m_World = info.m_World,
			.m_AssetManager = info.m_AssetManager,
			.m_TransferManager = *info.m_Renderer.GetTransferManager(),
			.m_RenderResourceRegistry = *info.m_Renderer.GetRenderResourceRegistry(),
			.m_RenderViews = std::span<RenderView>(result.m_RenderViews),
			.m_SceneCB = *info.m_Renderer.GetSceneConstantBuffer(),
			.m_ObjectsSB = *info.m_Renderer.GetObjectStructuredBuffer(),
			.m_MaterialsSB = *info.m_Renderer.GetMaterialStructuredBuffer(),
			.m_ViewsSB = *info.m_Renderer.GetViewStructuredBuffer(),
			.m_CurrentBackBufferIndex = info.m_BackBufferIndex
		};
		auto sceneBuildResult = m_SceneBuilder.Build(sceneBuildInfo);
		result.m_RenderScene = std::move(sceneBuildResult.m_RenderScene);
		result.m_UploadFencePoint = sceneBuildResult.m_UploadFencePoint;

		for (const RenderView& renderView : result.m_RenderViews)
		{
			if (renderView.m_ViewId == RenderViewID::Unknown)
			{
				continue;
			}

			const RenderQueueBuilder::BuildInfo queueBuildInfo{
				.m_AssetManager = info.m_AssetManager,
				.m_RenderScene = result.m_RenderScene,
				.m_RenderView = renderView
			};
			result.m_RenderQueues[utils::ToIndex(renderView.m_ViewId)] = m_QueueBuilder.Build(queueBuildInfo);
		}

		return result;
	}
}
