#pragma once
#include "DX12FencePoint.h"

#include <cstdint>

namespace gglab
{
	struct RenderView;
	struct RenderScene;
	class Renderer;
	class AssetManager;
	class ShaderManager;

	struct RenderFrameContext
	{
		const RenderView* m_RenderView = nullptr;
		const RenderScene* m_RenderScene = nullptr;

		uint32_t m_BackBufferIndex = 0;

		DX12FencePoint m_UploadFencePoint{};	// TODO: std::vector save wait fences

		bool IsValid() const noexcept
		{
			return m_RenderView && m_RenderScene;
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