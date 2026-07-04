#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiBackendFactory.h"
#include "DevTools/DevelopGui/Backends/DX12/DevelopGuiDX12Backend.h"
#include "Graphics/RHI/DX12/DX12Context.h"

namespace gglab
{
	std::unique_ptr<DevelopGuiBackend> CreateDevelopGuiBackend(
		const DevelopGuiBackend::CreateInfo& createInfo) noexcept
	{
		GGLAB_ASSERT(createInfo.m_RHIContext);
		if (!createInfo.m_RHIContext)
		{
			GGLAB_LOG_GRAPHICS_ERROR("Cannot create a DevelopGui backend without an RHI context.");
			return nullptr;
		}

		if (dynamic_cast<DX12Context*>(createInfo.m_RHIContext))
		{
			auto backend = std::make_unique<DevelopGuiDX12Backend>();
			if (!backend->Initialize(createInfo))
			{
				GGLAB_LOG_GRAPHICS_WARN("Failed to initialize the DevelopGui DX12 backend.");
				return nullptr;
			}
			return backend;
		}

		GGLAB_LOG_GRAPHICS_WARN("No DevelopGui backend is registered for the current RHI backend.");
		return nullptr;
	}
}
