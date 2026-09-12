#pragma once

#include "GGLabRuntime/Graphics/RHI/RHIFence.h"
#include "GGLabRuntime/Graphics/RenderSceneTypes.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

namespace gglab
{
	class AssetManager;
	class DebugDrawContext;
	class EnvironmentAssetController;
	class ApplicationInput;
	class RenderHost;
	class ShaderManager;
	class TaskSystem;
	class Time;

	struct DemoServices
	{
		// Optional Runtime host handle for AppRuntime-owned hosting code. Content
		// keeps using the concrete service surfaces it already receives.
		RenderHost* m_RenderHost = nullptr;
		// Stable explicit pass/content service bundle for content that no longer
		// needs the concrete renderer.
		RenderServices m_RenderServices{};
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		TaskSystem* m_TaskSystem = nullptr;
		const ApplicationInput* m_Input = nullptr;
		Time* m_Time = nullptr;
		DebugDrawContext* m_DebugDraw = nullptr;
		EnvironmentAssetController* m_EnvironmentAssetController = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_RenderHost && m_AssetManager && m_ShaderManager && m_TaskSystem &&
				m_Input && m_Time && m_DebugDraw && m_EnvironmentAssetController;
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
