#include "GGLabRuntime/Graphics/RenderHost.h"
#include "Graphics/Renderer.h"

namespace gglab
{
	std::unique_ptr<RenderHost> CreateRenderHost(const RenderHostCreateInfo& createInfo) noexcept
	{
		if (createInfo.m_RHIContextFactory == nullptr || !createInfo.HasRequiredRuntimePaths())
		{
			return nullptr;
		}

		auto renderer = std::make_unique<Renderer>();
		const Renderer::CreateInfo rendererCreateInfo{
			.m_RHIContextFactory = createInfo.m_RHIContextFactory,
			.m_ShaderManager = createInfo.m_ShaderManager,
			.m_TaskSystem = createInfo.m_TaskSystem,
			.m_IblDerivedDataCacheDirectory = createInfo.m_IblDerivedDataCacheDirectory,
			.m_Width = createInfo.m_Width,
			.m_Height = createInfo.m_Height,
			.m_AdapterSelector = createInfo.m_AdapterSelector,
			.m_EnableDebugValidation = createInfo.m_EnableDebugValidation,
		};
		if (!renderer->Initialize(rendererCreateInfo))
		{
			return nullptr;
		}
		return renderer;
	}
}
