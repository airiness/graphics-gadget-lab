#pragma once
#include "Graphics/RHI/DX12/DX12FencePoint.h"
#include "Graphics/RenderQueue.h"
#include "Graphics/RenderScene.h"
#include "Graphics/RenderView.h"
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

	struct RenderFrameContext
	{
		std::span<RenderView> m_RenderViews;
		const RenderScene& m_RenderScene;
		std::span<const RenderQueue> m_RenderQueues;

		DirectionalShadowSettings m_DirectionalShadowSettings = DisabledDirectionalShadowSettings();
		const ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;

		uint32_t m_BackBufferIndex = 0;

		DX12FencePoint m_UploadFencePoint{};	// TODO: multi fence points support
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

		const DirectionalShadowSettings& GetDirectionalShadowSettings() const noexcept
		{
			return m_DirectionalShadowSettings;
		}

		const ShadowVisualizationSettings& GetShadowVisualizationSettings() const noexcept
		{
			return m_ShadowVisualizationSettings ? *m_ShadowVisualizationSettings : DefaultShadowVisualizationSettings();
		}

		bool IsValid() const noexcept
		{
			return (m_RenderViews.size() >= utils::ToIndex(RenderViewID::Count)) &&
				(m_RenderQueues.size() >= utils::ToIndex(RenderViewID::Count)) &&
				m_UploadFencePoint.IsValid();
		}
	};

	struct RenderServices
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;

		bool IsValid() const noexcept
		{
			return m_Renderer && m_AssetManager && m_ShaderManager;
		}
	};
}
