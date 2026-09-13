#pragma once

#include "GGLabRuntime/Graphics/EnvironmentLightingControlBase.h"
#include "GGLabRuntime/Graphics/EnvironmentLightingViewBase.h"
#include "GGLabRuntime/Graphics/IBLCacheControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingControlBase.h"
#include "GGLabRuntime/Graphics/Profiling/GpuProfilingViewBase.h"
#include "GGLabRuntime/Graphics/RHI/RHIContext.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

namespace gglab
{
	class AssetManager;
	class AssetUploadControl;
	class DebugDrawContext;
	class EnvironmentAssetController;
	class ApplicationInput;
	class ShaderManager;
	class TaskSystem;
	class Time;

	struct DemoServices
	{
		// Stable explicit pass/content service bundle for content that no longer
		// needs the concrete renderer. IsValid() validates the required
		// capabilities; the optional capabilities are explicitly nullable.
		RenderServices m_RenderServices{};
		EnvironmentLightingViewBase* m_EnvironmentLighting = nullptr;
		EnvironmentLightingControlBase* m_EnvironmentLightingControl = nullptr;
		IBLCacheControlBase* m_IBLCacheControl = nullptr;
		GpuProfilingViewBase* m_GpuProfiling = nullptr;
		GpuProfilingControlBase* m_GpuProfilingControl = nullptr;
		AssetUploadControl* m_AssetUploadControl = nullptr;
		RHIContext* m_RHIContext = nullptr;
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		const ApplicationInput* m_Input = nullptr;
		Time* m_Time = nullptr;
		DebugDrawContext* m_DebugDraw = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			// Required: the explicit render service bundle plus every content
			// service below. The renderer always provides the RHI context,
			// environment lighting and IBL cache when composition succeeds.
			// Optional: GPU profiling depends on backend profiler availability;
			// the asset upload control is a developer/acceptance capability.
			return m_RenderServices.IsValid() && m_RHIContext && m_EnvironmentLighting &&
				m_EnvironmentLightingControl && m_IBLCacheControl && m_AssetManager &&
				m_ShaderManager && m_TaskSystem && m_Input && m_Time && m_DebugDraw &&
				m_EnvironmentAssetController;
		}
	};

	struct DemoCreateInfo
	{
		DemoServices m_Services{};
		uint32_t m_WindowWidth = 0;
		uint32_t m_WindowHeight = 0;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Services.IsValid() && m_WindowWidth > 0 && m_WindowHeight > 0;
		}
	};

	struct DemoFrameFeedback
	{
		RenderSceneBuildStatus m_RenderSceneStatus = RenderSceneBuildStatus::GpuUploadFailed;
		RHIFencePoint m_SubmittedFence{};
		uint64_t m_FrameIndex = 0;
		uint32_t m_BackBufferIndex = 0;
	};
}
