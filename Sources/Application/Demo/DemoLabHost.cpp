#include "Core/Precompiled.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Lab/Sessions/HelloLabSession.h"

namespace gglab
{
	DemoLabHost::DemoLabHost(const DemoCreateInfo& createInfo) noexcept :
		m_Runtime({
			.m_Services = createInfo.m_Services,
			.m_WindowWidth = createInfo.m_WindowWidth,
			.m_WindowHeight = createInfo.m_WindowHeight,
		})
	{
		const bool registered = m_Runtime.RegisterLab(
			HelloLabSession::GetDescriptor(),
			&HelloLabSession::Create);
		GGLAB_ASSERT_MSG(registered, "Failed to register the Hello Lab session.");

		const bool initialized = m_Runtime.Initialize(HelloLabSession::GetId());
		GGLAB_ASSERT_MSG(initialized, "Failed to initialize the Lab runtime.");
	}

	void DemoLabHost::OnEnter() noexcept
	{
		m_Runtime.OnEnter();
	}

	void DemoLabHost::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_Runtime.OnResize(width, height);
	}

	void DemoLabHost::OnExit() noexcept
	{
		m_Runtime.OnExit();
	}

	void DemoLabHost::Update() noexcept
	{
		m_Runtime.ProcessPendingCommands();
		m_Runtime.Update();
	}

	void DemoLabHost::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		m_Runtime.OnFrameSubmitted(feedback);
	}
}
