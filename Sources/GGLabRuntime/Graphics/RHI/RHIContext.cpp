#include "Graphics/RHI/RHIContext.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/DX12/DX12Context.h"
#if GGLAB_ENABLE_VULKAN
#include "Graphics/RHI/Vulkan/VulkanContext.h"
#endif

#include <memory>

namespace gglab
{
	std::unique_ptr<RHIContext> CreateRHIContext(const RHIContextDesc& desc) noexcept
	{
		switch (desc.m_Backend)
		{
		case RHIBackendType::DX12:
			return std::make_unique<DX12Context>(desc);
		case RHIBackendType::Vulkan:
#if GGLAB_ENABLE_VULKAN
			return VulkanContext::Create(desc);
#else
			GGLAB_LOG_GRAPHICS_ERROR_ALWAYS(
				"The Vulkan RHI was requested, but this build has GGLAB_ENABLE_VULKAN=0.");
			return {};
#endif
		default:
			GGLAB_LOG_GRAPHICS_ERROR("CreateRHIContext received an unsupported RHI backend.");
			return {};
		}
	}
}
