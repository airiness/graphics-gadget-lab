#include "AppRuntimeConfig.h"

namespace gglab
{
	bool AppRuntimeConfig::IsValid() const noexcept
	{
		if (m_RhiBackend != AppRuntimeRHIBackend::DX12 &&
			m_RhiBackend != AppRuntimeRHIBackend::Vulkan)
		{
			return false;
		}
		if (m_InitialExtent.m_Width == 0 || m_InitialExtent.m_Height == 0)
		{
			return false;
		}
		if (m_AdapterSelector &&
			(m_AdapterSelector->empty() || m_RhiBackend != AppRuntimeRHIBackend::Vulkan))
		{
			return false;
		}
		if (m_StartupLabId &&
			(m_StartupLabId->empty() || m_StartupDemo != AppRuntimeStartupDemo::LabHost))
		{
			return false;
		}

		switch (m_StartupDemo)
		{
		case AppRuntimeStartupDemo::Start:
		case AppRuntimeStartupDemo::Playground:
		case AppRuntimeStartupDemo::LabHost:
			break;
		default:
			return false;
		}

		switch (m_InitialPointerMode)
		{
		case AppRuntimePointerMode::Relative:
		case AppRuntimePointerMode::Absolute:
			return true;
		default:
			return false;
		}
	}

	bool AppRuntimeConfig::HasCapability(AppRuntimeCapability capability) const noexcept
	{
		const uint32_t capabilities = static_cast<uint32_t>(m_Capabilities);
		const uint32_t requested = static_cast<uint32_t>(capability);
		return requested != 0 && (capabilities & requested) == requested;
	}
}
