#include "Application/Demo/DemoLabHost.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <format>
#include <string>

namespace gglab
{
	DemoLabHost::DemoLabHost(const DemoCreateInfo& createInfo, const LabId& startupLab,
		std::span<const LabRegistration> labRegistrations) noexcept :
		m_StartupLab(startupLab),
		m_Runtime({
			.m_Services = createInfo.m_Services,
			.m_WindowWidth = createInfo.m_WindowWidth,
			.m_WindowHeight = createInfo.m_WindowHeight,
			})
	{
		for (const LabRegistration& registration : labRegistrations)
		{
			m_RegistrationsValid = m_Runtime.RegisterLab(
				registration.m_Descriptor, registration.m_Factory) && m_RegistrationsValid;
		}
	}

	void DemoLabHost::BeginPrepare() noexcept
	{
		GGLAB_UNUSED(m_Runtime.Initialize(m_StartupLab));
	}

	void DemoLabHost::TickPrepare() noexcept
	{
		m_Runtime.TickTransitions();
	}

	LoadingProgress DemoLabHost::GetPreparationProgress() const noexcept
	{
		if (const auto progress = m_Runtime.GetLoadingProgress())
		{
			return *progress;
		}
		if (m_Runtime.IsInitialized())
		{
			return LoadingProgress::Ready();
		}
		if (m_Runtime.GetState() == LabRunState::Failed)
		{
			return {
				.m_Status = LoadingStatus::Failed,
				.m_Fraction = 0.0f,
				.m_Stage = "Lab initialization failed",
				.m_Detail = std::string(m_Runtime.GetLastError()),
			};
		}
		return {
			.m_Status = LoadingStatus::Preparing,
			.m_Fraction = 0.0f,
			.m_Stage = "Initializing Lab runtime",
			.m_Detail = std::string(m_StartupLab.GetName()),
		};
	}

	void DemoLabHost::CommitPrepare() noexcept
	{
		GGLAB_ASSERT_MSG(m_Runtime.IsInitialized(), "DemoLabHost requires a prepared Lab session.");
	}

	void DemoLabHost::CancelPrepare() noexcept
	{
		m_Runtime.Shutdown();
	}

	std::optional<LoadingProgress> DemoLabHost::GetActiveLoadingProgress() const noexcept
	{
		return m_Runtime.GetLoadingProgress();
	}

	void DemoLabHost::AppendCaptureReadiness(FrameCaptureReadiness& readiness) const noexcept
	{
		DemoBase::AppendCaptureReadiness(readiness);
		if (m_Runtime.HasPendingSession())
		{
			readiness.Add("lab", FrameCaptureGateState::Pending, "Switching Lab sessions.");
			return;
		}
		switch (m_Runtime.GetState())
		{
		case LabRunState::Ready:
			readiness.Add("lab", FrameCaptureGateState::Ready);
			return;
		case LabRunState::Failed:
			readiness.Add(
				"lab", FrameCaptureGateState::Failed, std::string(m_Runtime.GetLastError()));
			return;
		case LabRunState::WarmingUp:
			readiness.Add("lab", FrameCaptureGateState::Pending,
				std::format("Warming up, {} frames remaining.",
					m_Runtime.GetWarmupFramesRemaining()));
			return;
		default:
			readiness.Add("lab", FrameCaptureGateState::Pending, "Loading the Lab session.");
			return;
		}
	}

	std::string DemoLabHost::GetCaptureContentId() const noexcept
	{
		const LabSessionBase* session = m_Runtime.GetActiveSession();
		return session ? std::string(session->GetDescriptor().m_Id.GetName()) : std::string{};
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

	void DemoLabHost::OnCameraPathFrameApplied(const CameraPath& path, uint32_t frame) noexcept
	{
		m_Runtime.OnCameraPathFrameApplied(path, frame);
	}
}
