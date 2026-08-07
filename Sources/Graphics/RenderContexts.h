#pragma once
#include "Graphics/RHI/RHIFence.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderView.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/ShadowSettings.h"

#include <cstdint>
#include <span>

namespace gglab
{
	struct RenderView;
	struct RenderQueue;
	class Renderer;
	class AssetManager;
	class ShaderManager;
	class DevelopGuiSystem;

	struct RenderFrameContext
	{
		std::span<RenderView> m_RenderViews;
		std::span<const ResolvedViewRenderSettings> m_ViewRenderSettings;
		RenderViewID m_DisplayViewId = RenderViewID::Main;
		const RenderScene& m_RenderScene;
		std::span<const RenderQueue> m_RenderQueues;
		DebugDrawFrameView m_DebugDrawFrame{};

		DirectionalShadowSettings m_DirectionalShadowSettings = DisabledDirectionalShadowSettings();
		const ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;

		uint32_t m_FrameSlotIndex = 0;
		uint32_t m_BackBufferIndex = 0;
		uint64_t m_FrameSerial = 0;

		RHIFencePoint m_UploadFencePoint{}; // TODO: multi fence points support
		RenderSceneGpuAllocations* m_SceneGpuAllocations = nullptr;
		RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;

		bool IsRenderSceneReady() const noexcept
		{
			return m_RenderSceneStatus == RenderSceneBuildStatus::Ready;
		}

		const RenderQueue& GetRenderQueue(RenderViewID viewId) const noexcept
		{
			const auto index = utils::ToIndex(viewId);
			GGLAB_ASSERT(index < m_RenderQueues.size());
			return m_RenderQueues[index];
		}

		RenderViewID GetDisplayViewId() const noexcept { return m_DisplayViewId; }

		const RenderView& GetDisplayRenderView() const noexcept
		{
			const auto index = utils::ToIndex(m_DisplayViewId);
			GGLAB_ASSERT(index < m_RenderViews.size());
			return m_RenderViews[index];
		}

		const ResolvedViewRenderSettings& GetViewRenderSettings(RenderViewID viewId) const noexcept
		{
			const auto index = utils::ToIndex(viewId);
			GGLAB_ASSERT(index < m_ViewRenderSettings.size());
			return m_ViewRenderSettings[index];
		}

		const ResolvedViewRenderSettings& GetDisplayViewRenderSettings() const noexcept
		{
			return GetViewRenderSettings(m_DisplayViewId);
		}

		const DirectionalShadowSettings& GetDirectionalShadowSettings() const noexcept
		{
			return m_DirectionalShadowSettings;
		}

		const ShadowVisualizationSettings& GetShadowVisualizationSettings() const noexcept
		{
			return m_ShadowVisualizationSettings ? *m_ShadowVisualizationSettings
				: DefaultShadowVisualizationSettings();
		}

		bool IsValid() const noexcept
		{
			return m_FrameSerial != 0 &&
				(m_RenderViews.size() >= utils::ToIndex(RenderViewID::Count)) &&
				(m_ViewRenderSettings.size() >= utils::ToIndex(RenderViewID::Count)) &&
				(m_RenderQueues.size() >= utils::ToIndex(RenderViewID::Count));
		}
	};

	struct RenderServices
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		DevelopGuiSystem* m_DevelopGuiSystem = nullptr;

		bool IsValid() const noexcept { return m_Renderer && m_AssetManager && m_ShaderManager; }
	};
}
