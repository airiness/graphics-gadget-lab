#pragma once
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderView.h"
#include "Graphics/RenderWorldExtractor.h"

namespace gglab
{
	class AssetManager;
	class Camera;
	class Renderer;
	class World;

	class RenderFrameBuilder
	{
	public:
		struct BuildInfo
		{
			World& m_World;
			Camera& m_Camera;
			Renderer& m_Renderer;
			AssetManager& m_AssetManager;
			ShadowVisualizationSettings& m_ShadowVisualizationSettings;
			uint32_t m_WindowWidth = 0;
			uint32_t m_WindowHeight = 0;
			uint32_t m_BackBufferIndex = 0;
		};

		struct BuildResult
		{
			RenderWorldData m_WorldData{};
			std::vector<RenderView> m_RenderViews;
			RenderScene m_RenderScene{};
			RenderSceneGpuAllocations m_SceneGpuAllocations{};
			std::array<RenderQueue, utils::ToIndex(RenderViewID::Count)> m_RenderQueues{};
			DX12FencePoint m_UploadFencePoint{};
			RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;
			uint32_t m_BackBufferIndex = 0;
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
