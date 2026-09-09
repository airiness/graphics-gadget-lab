#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevelopGui/DevelopGuiStateStore.h"
#include "GGLabRuntime/Graphics/DebugDraw/DebugDraw.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

namespace gglab
{
	class WorldToolingViewBase;
	class WorldToolingControlBase;
	class DirectionalLightViewBase;
	class DirectionalLightControlBase;
	class CameraToolingViewBase;
	class CameraToolingControlBase;
	class CameraRenderViewQueryBase;
	class DX12ResourceLifecycleViewBase;
	class DX12ResourceLifecycleControlBase;
	class AssetToolingControlBase;
	class EnvironmentSelectionControlBase;
	class EnvironmentLightingControlBase;
	class EnvironmentLightingViewBase;
	class GpuProfilingControlBase;
	class GpuProfilingViewBase;
	class IBLCacheControlBase;
	class IBLPreviewViewBase;
	class IBLPreviewControlBase;
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
		const CameraToolingViewBase* m_Cameras = nullptr;
		CameraToolingControlBase* m_CameraControl = nullptr;
		const CameraRenderViewQueryBase* m_CameraRenderViewQuery = nullptr;
		// Borrowed from the host's optional DX12 tooling adapter for this draw.
		const DX12ResourceLifecycleViewBase* m_DX12ResourceLifecycle = nullptr;
		DX12ResourceLifecycleControlBase* m_DX12ResourceLifecycleControl = nullptr;
		const WorldToolingViewBase* m_WorldView = nullptr;
		WorldToolingControlBase* m_WorldControl = nullptr;
		const DirectionalLightViewBase* m_DirectionalLight = nullptr;
		DirectionalLightControlBase* m_DirectionalLightControl = nullptr;
		AssetToolingControlBase* m_AssetControl = nullptr;
		EnvironmentSelectionControlBase* m_EnvironmentSelectionControl = nullptr;
		DiagnosticsView* m_Diagnostics = nullptr;
		DiagnosticsControl* m_DiagnosticsControl = nullptr;
		// Borrowed for this draw only; panels may retain copied timing values.
		const EnvironmentLightingViewBase* m_EnvironmentLighting = nullptr;
		EnvironmentLightingControlBase* m_EnvironmentLightingControl = nullptr;
		const GpuProfilingViewBase* m_GpuProfiling = nullptr;
		GpuProfilingControlBase* m_GpuProfilingControl = nullptr;
		IBLCacheControlBase* m_IBLCacheControl = nullptr;
		const IBLPreviewViewBase* m_IBLPreview = nullptr;
		IBLPreviewControlBase* m_IBLPreviewControl = nullptr;
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
