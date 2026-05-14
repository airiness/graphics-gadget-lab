#pragma once
#include "Graphics/DX12/DX12FencePoint.h"
#include "Graphics/RenderView.h"

#include <cstdint>
#include <span>

namespace gglab
{
	struct RenderView;
	struct RenderScene;
	struct RenderQueue;
	class Renderer;
	class AssetManager;
	class ShaderManager;

	struct RenderFrameContext
	{
		std::span<RenderView> m_RenderViews;
		const RenderScene& m_RenderScene;
		const RenderQueue& m_RenderQueue;

		uint32_t m_BackBufferIndex = 0;

		DX12FencePoint m_UploadFencePoint{};	// TODO: multi fence points support

		bool IsValid() const noexcept
		{
			return (m_RenderViews.size() > 0) && m_UploadFencePoint.IsValid();
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