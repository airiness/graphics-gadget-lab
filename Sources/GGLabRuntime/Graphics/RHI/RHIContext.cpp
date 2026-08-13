#include "Graphics/RHI/RHIContext.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/RHI/DX12/DX12Context.h"

#include <memory>

namespace gglab
{
	std::unique_ptr<RHIContext> CreateRHIContext(const RHIContextDesc& desc) noexcept
	{
		switch (desc.m_Backend)
		{
		case RHIBackendType::DX12:
			return std::make_unique<DX12Context>(desc);
		default:
			GGLAB_LOG_GRAPHICS_ERROR("CreateRHIContext received an unsupported RHI backend.");
			return {};
		}
	}
}
