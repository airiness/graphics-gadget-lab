#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "Graphics/DebugDraw/DebugDraw.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderView.h"
#include "Graphics/PostProcess/ViewRenderSettings.h"
#include "Graphics/Pipeline/TemporalAA.h"
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
	class TemporalFrameTransaction;
	class RenderPipelineOverlayExtensionBase;
	struct RenderFrameContext;

	struct RenderFrameGpuResources
	{
		RHIFencePoint m_UploadFencePoint{};
		RenderSceneGpuAllocations m_SceneGpuAllocations{};

		void AdoptFrom(const RenderFrameContext& context) noexcept;
		bool IsEmpty() const noexcept
		{
			return !m_UploadFencePoint.IsValid() && m_SceneGpuAllocations.IsEmpty();
		}
		void Reset() noexcept { *this = {}; }
	};

	struct RenderFrameContext
	{
		std::span<RenderView> m_RenderViews;
		std::span<const ResolvedViewRenderSettings> m_ViewRenderSettings;
		ResolvedTemporalFramePlan m_TemporalFramePlan{};
		TemporalFrameTransaction* m_TemporalFrameTransaction = nullptr;
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

		const ResolvedTemporalFramePlan& GetTemporalFramePlan() const noexcept
		{
			return m_TemporalFramePlan;
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

	inline void RenderFrameGpuResources::AdoptFrom(const RenderFrameContext& context) noexcept
	{
		if (context.m_UploadFencePoint.IsValid())
		{
			GGLAB_ASSERT_MSG(!m_UploadFencePoint.IsValid() ||
				m_UploadFencePoint == context.m_UploadFencePoint,
				"A render frame cannot adopt resources from different upload submissions.");
			m_UploadFencePoint = context.m_UploadFencePoint;
		}
		if (!context.m_SceneGpuAllocations || context.m_SceneGpuAllocations->IsEmpty())
		{
			return;
		}

		GGLAB_ASSERT_MSG(m_SceneGpuAllocations.IsEmpty(),
			"A render frame cannot replace scene GPU allocations before retirement.");
		if (!m_SceneGpuAllocations.IsEmpty())
		{
			return;
		}
		m_SceneGpuAllocations = *context.m_SceneGpuAllocations;
		*context.m_SceneGpuAllocations = {};
	}

	struct RenderServices
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		RenderPipelineOverlayExtensionBase* m_OverlayExtension = nullptr;

		bool IsValid() const noexcept { return m_Renderer && m_AssetManager && m_ShaderManager; }
	};
}
