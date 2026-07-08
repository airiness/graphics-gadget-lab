#include "Core/Precompiled.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Lab/Sessions/AlphaTestLabSession.h"
#include "Application/Lab/Sessions/CullingLabSession.h"
#include "Application/Lab/Sessions/MathFoundationLabSession.h"
#include "Application/Lab/Sessions/MiniPBRGridLabSession.h"

namespace gglab
{
	DemoLabHost::DemoLabHost(
		const DemoCreateInfo& createInfo,
		const LabId& startupLab) noexcept :
		m_Runtime({
			.m_Services = createInfo.m_Services,
			.m_WindowWidth = createInfo.m_WindowWidth,
			.m_WindowHeight = createInfo.m_WindowHeight,
		})
	{
		const bool registered = m_Runtime.RegisterLab(
			CullingLabSession::GetDescriptor(),
			&CullingLabSession::Create);
		GGLAB_ASSERT_MSG(registered, "Failed to register the Culling Lab session.");

		const bool miniPbrRegistered = m_Runtime.RegisterLab(
			MiniPBRGridLabSession::GetDescriptor(),
			&MiniPBRGridLabSession::Create);
		GGLAB_ASSERT_MSG(miniPbrRegistered, "Failed to register the Mini PBR Grid Lab session.");

		const bool alphaTestRegistered = m_Runtime.RegisterLab(
			AlphaTestLabSession::GetDescriptor(),
			&AlphaTestLabSession::Create);
		GGLAB_ASSERT_MSG(alphaTestRegistered, "Failed to register the Alpha Test Lab session.");

		const bool mathFoundationRegistered = m_Runtime.RegisterLab(
			MathFoundationLabSession::GetDescriptor(),
			&MathFoundationLabSession::Create);
		GGLAB_ASSERT_MSG(mathFoundationRegistered,
			"Failed to register the Math Foundation Lab session.");

		GGLAB_UNUSED(m_Runtime.Initialize(startupLab));
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
