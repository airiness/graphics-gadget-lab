#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiStateStore.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDraw.h"
#include "GGLabRuntime/Graphics/RenderQueue.h"
#include "GGLabRuntime/Graphics/RenderView.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

namespace gglab
{
	class World;
	class CameraRig;
	class Renderer;
	class AssetManager;
	class EnvironmentAssetController;
	class GpuProfilingControlBase;
	class GpuProfilingViewBase;
	class PostProcessPreviewControlBase;
	class PostProcessPreviewViewBase;
	class ShadowPreviewViewBase;
	class DiagnosticsControl;
	class DiagnosticsView;
	class DevelopGuiSystem;
	class DebugDrawSystem;
	struct ViewRenderSettingsOverrides;

	class DevelopGuiStateStore;

	struct DevelopGuiContext
	{
		CameraRig* m_CameraRig = nullptr;
		Renderer* m_Renderer = nullptr;
		World* m_World = nullptr;
		std::span<RenderView> m_RenderViews;
		std::span<const RenderQueue> m_RenderQueues;
		AssetManager* m_AssetManager = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;
		DiagnosticsView* m_Diagnostics = nullptr;
		DiagnosticsControl* m_DiagnosticsControl = nullptr;
		// Borrowed for this draw only; panels may retain copied timing values.
		const GpuProfilingViewBase* m_GpuProfiling = nullptr;
		GpuProfilingControlBase* m_GpuProfilingControl = nullptr;
		const PostProcessPreviewViewBase* m_PostProcessPreview = nullptr;
		PostProcessPreviewControlBase* m_PostProcessPreviewControl = nullptr;
		const ShadowPreviewViewBase* m_ShadowPreview = nullptr;
		DebugDrawSystem* m_DebugDrawSystem = nullptr;
		DebugDrawFrameView m_DebugDrawFrame{};
		ShadowVisualizationSettings* m_ShadowVisualizationSettings = nullptr;
		ViewRenderSettingsOverrides* m_ViewRenderSettingsOverrides = nullptr;
		DevelopGuiSystem* m_DevelopGuiSystem = nullptr;

		DevelopGuiStateStore* m_StateStore = nullptr;
		uint64_t m_CurrentPanelKey = 0;

		template <typename T, typename... ARGS> T& PanelState(ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(m_CurrentPanelKey, std::forward<ARGS>(args)...);
		}

		template <typename T, typename... ARGS> T& StateFor(uint64_t key, ARGS&&... args) noexcept
		{
			GGLAB_ASSERT(m_StateStore);
			return m_StateStore->GetOrCreate<T>(key, std::forward<ARGS>(args)...);
		}
	};
}
