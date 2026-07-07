#pragma once

#include "Graphics/RHI/RHIFence.h"
#include "Graphics/RenderScene.h"

namespace gglab
{
	class AssetManager;
	class DebugDrawContext;
	class InputManager;
	class Renderer;
	class ShaderManager;
	class Time;

	struct DemoServices
	{
		Renderer* m_Renderer = nullptr;
		AssetManager* m_AssetManager = nullptr;
		ShaderManager* m_ShaderManager = nullptr;
		InputManager* m_InputManager = nullptr;
		Time* m_Time = nullptr;
		DebugDrawContext* m_DebugDraw = nullptr;

		[[nodiscard]] bool IsValid() const noexcept
		{
			return m_Renderer && m_AssetManager && m_ShaderManager && m_InputManager &&
				m_Time && m_DebugDraw;
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
