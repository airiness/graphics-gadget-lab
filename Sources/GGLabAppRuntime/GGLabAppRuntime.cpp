#include "GGLabAppRuntime.h"

namespace gglab
{
	GGLabAppRuntime::~GGLabAppRuntime() noexcept
	{
		Shutdown();
	}

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
			Shutdown();
			return AppRuntimeInitializeResult::InvalidConfig;
		}
		if (!createInfo.m_Paths.IsValid())
		{
			m_LifecycleState = AppRuntimeLifecycleState::Failed;
			Shutdown();
			return AppRuntimeInitializeResult::InvalidRuntimePaths;
		}

		m_BootstrapService = createInfo.m_BootstrapService;
		if (!m_BootstrapService)
		{
			m_LifecycleState = AppRuntimeLifecycleState::Failed;
			Shutdown();
			return AppRuntimeInitializeResult::MissingBootstrapService;
		}

		m_BootstrapInitializationAttempted = true;
		if (!m_BootstrapService->Initialize(
			createInfo.m_Config, createInfo.m_Paths, createInfo.m_HostServices))
		{
			m_LifecycleState = AppRuntimeLifecycleState::Failed;
			Shutdown();
			return AppRuntimeInitializeResult::BootstrapServiceFailed;
		}

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

	void GGLabAppRuntime::Shutdown() noexcept
	{
		if (m_ShutdownComplete ||
			m_LifecycleState == AppRuntimeLifecycleState::ShuttingDown)
		{
			return;
		}

		const bool preserveFailure = m_LifecycleState == AppRuntimeLifecycleState::Failed;
		m_LifecycleState = AppRuntimeLifecycleState::ShuttingDown;
		if (m_BootstrapInitializationAttempted && m_BootstrapService)
		{
			m_BootstrapService->Shutdown();
			m_BootstrapInitializationAttempted = false;
		}
		m_BootstrapService = nullptr;

		m_ShutdownComplete = true;
		m_LifecycleState = preserveFailure
			? AppRuntimeLifecycleState::Failed
			: AppRuntimeLifecycleState::Stopped;
	}
}
