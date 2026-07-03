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

		if (dynamic_cast<DX12Context*>(createInfo.m_RHIContext))
		{
			auto backend = std::make_unique<DevelopGuiDX12Backend>();
			backend->Initialize(createInfo);
			return backend;
		}

		GGLAB_ASSERT_MSG(false, "No DevelopGui backend is registered for the current RHI backend.");
		return nullptr;
	}
}
