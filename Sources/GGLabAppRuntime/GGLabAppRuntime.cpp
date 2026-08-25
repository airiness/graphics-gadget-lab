#include "GGLabAppRuntime.h"

#include "Demo/DemoManager.h"
#include "Graphics/Renderer.h"

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

	void GGLabAppRuntime::HandleHostEvent(const AppHostEvent& event) noexcept
	{
		switch (event.m_Type)
		{
		case AppHostEventType::None:
			break;
		case AppHostEventType::Suspended:
			if (m_LifecycleState == AppRuntimeLifecycleState::Running)
			{
				if (m_Renderer)
				{
					m_Renderer->OnSuspend();
				}
				m_LifecycleState = AppRuntimeLifecycleState::Suspended;
			}
			break;
		case AppHostEventType::Resumed:
			if (m_LifecycleState == AppRuntimeLifecycleState::Suspended)
			{
				if (m_Renderer)
				{
					m_Renderer->OnResume();
				}
				if (m_ResizePending)
				{
					m_Renderer->OnResize(m_WindowWidth, m_WindowHeight);
					m_DemoManager->OnResize(m_WindowWidth, m_WindowHeight);
					m_ResizePending = false;
				}
				m_LifecycleState = AppRuntimeLifecycleState::Running;
			}
			break;
		case AppHostEventType::Resized:
			if (m_LifecycleState == AppRuntimeLifecycleState::Running ||
				m_LifecycleState == AppRuntimeLifecycleState::Suspended)
			{
				Resize(event.m_Width, event.m_Height);
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

	void GGLabAppRuntime::Resize(uint32_t width, uint32_t height) noexcept
	{
		if (!m_ServicesInitialized || width == 0 || height == 0 ||
			(width == m_WindowWidth && height == m_WindowHeight))
		{
			return;
		}

		m_WindowWidth = width;
		m_WindowHeight = height;
		if (m_LifecycleState == AppRuntimeLifecycleState::Suspended)
		{
			m_ResizePending = true;
			return;
		}

		m_Renderer->OnResize(width, height);
		m_DemoManager->OnResize(width, height);
		m_ResizePending = false;
	}
}
