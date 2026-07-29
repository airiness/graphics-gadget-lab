#pragma once
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderWorldExtractor.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"

namespace gglab
{
	class AssetManager;
	class CameraRig;
	class Renderer;
	class World;

	class RenderFrameBuilder
	{
	public:
		struct BuildInfo
		{
			World& m_World;
			CameraRig& m_CameraRig;
			Renderer& m_Renderer;
			AssetManager& m_AssetManager;
			ShadowVisualizationSettings& m_ShadowVisualizationSettings;
			const ViewRenderProfile& m_ViewRenderProfile;
			uint32_t m_WindowWidth = 0;
			uint32_t m_WindowHeight = 0;
			uint32_t m_BackBufferIndex = 0;
			uint64_t m_FrameSerial = 0;
		};

		struct BuildResult
		{
			RenderWorldData m_WorldData{};
			std::vector<RenderView> m_RenderViews;
			std::array<ResolvedViewRenderSettings, utils::ToIndex(RenderViewID::Count)>
				m_ViewRenderSettings{};
			RenderScene m_RenderScene{};
			RenderSceneGpuAllocations m_SceneGpuAllocations{};
			std::array<RenderQueue, utils::ToIndex(RenderViewID::Count)> m_RenderQueues{};
			DebugDrawFrameView m_DebugDrawFrame{};
			DebugDrawCullContext m_DebugDrawCullContext{};
			RHIFencePoint m_UploadFencePoint{};
			RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;
			RenderViewID m_DisplayViewId = RenderViewID::Main;
			uint32_t m_BackBufferIndex = 0;
			uint64_t m_FrameSerial = 0;
			ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;

			RenderFrameContext MakeRenderFrameContext() noexcept;
		};

	public:
		BuildResult Build(const BuildInfo& info) noexcept;

	private:
		RenderWorldExtractor m_WorldExtractor;
		RenderViewBuilder m_ViewBuilder;
		RenderSceneBuilder m_SceneBuilder;
		RenderQueueBuilder m_QueueBuilder;
	};
}
