#include "GGLabAppRuntime.h"

namespace gglab
{
	AppRuntimeInitializeResult GGLabAppRuntime::Initialize(
		const GGLabAppRuntimeCreateInfo& createInfo) noexcept
	{
		if (m_LifecycleState == AppRuntimeLifecycleState::Running ||
			m_LifecycleState == AppRuntimeLifecycleState::Suspended ||
			m_LifecycleState == AppRuntimeLifecycleState::ExitRequested)
		{
			return AppRuntimeInitializeResult::AlreadyInitialized;
		}
		if (m_LifecycleState != AppRuntimeLifecycleState::Uninitialized)
		{
			return AppRuntimeInitializeResult::InvalidState;
		}

		m_LifecycleState = AppRuntimeLifecycleState::Initializing;
		if (!createInfo.m_Config.IsValid())
		{
			m_LifecycleState = AppRuntimeLifecycleState::Failed;
			return AppRuntimeInitializeResult::InvalidConfig;
		}
		if (!createInfo.m_Paths.IsValid())
		{
			m_LifecycleState = AppRuntimeLifecycleState::Failed;
			return AppRuntimeInitializeResult::InvalidRuntimePaths;
		}

		m_Config = createInfo.m_Config;
		m_Paths = createInfo.m_Paths;
		m_HostServices = createInfo.m_HostServices;

		m_LifecycleState = AppRuntimeLifecycleState::Running;
		return AppRuntimeInitializeResult::Succeeded;
	}

	AppRuntimeTickResult GGLabAppRuntime::Tick() const noexcept
	{
		switch (m_LifecycleState)
		{
		case AppRuntimeLifecycleState::Running:
			return AppRuntimeTickResult::Continue;
		case AppRuntimeLifecycleState::Suspended:
			return AppRuntimeTickResult::Suspended;
		default:
			return AppRuntimeTickResult::Exit;
		}
	}

	void GGLabAppRuntime::HandleHostEvent(AppHostEventType eventType) noexcept
	{
		switch (eventType)
		{
		case AppHostEventType::Suspended:
			if (m_LifecycleState == AppRuntimeLifecycleState::Running)
			{
				m_LifecycleState = AppRuntimeLifecycleState::Suspended;
			}
			break;
		case AppHostEventType::Resumed:
			if (m_LifecycleState == AppRuntimeLifecycleState::Suspended)
			{
				m_LifecycleState = AppRuntimeLifecycleState::Running;
			}
			break;
		case AppHostEventType::ExitRequested:
			if (m_LifecycleState == AppRuntimeLifecycleState::Running ||
				m_LifecycleState == AppRuntimeLifecycleState::Suspended)
			{
				m_LifecycleState = AppRuntimeLifecycleState::ExitRequested;
			}
			break;
		}
	}

}
