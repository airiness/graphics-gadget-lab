#include "Core/Precompiled.h"
#include "Application/Demo/DemoLabHost.h"
#include "Application/Lab/Sessions/AssetPublicationLabSession.h"
#include "Application/Lab/Sessions/AssetResidencyLabSession.h"
#include "Application/Lab/Sessions/EnvironmentAssetLabSession.h"
#include "Application/Lab/Sessions/AlphaTestLabSession.h"
#include "Application/Lab/Sessions/CullingLabSession.h"
#include "Application/Lab/Sessions/MathFoundationLabSession.h"
#include "Application/Lab/Sessions/MiniPBRGridLabSession.h"
#include "Application/Lab/Sessions/PostProcessLabSession.h"
#include "Application/Lab/Sessions/TaskSystemLabSession.h"

namespace gglab
{
	DemoLabHost::DemoLabHost(
		const DemoCreateInfo& createInfo,
		const LabId& startupLab) noexcept :
		m_StartupLab(startupLab),
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

		const bool postProcessRegistered = m_Runtime.RegisterLab(
			PostProcessLabSession::GetDescriptor(),
			&PostProcessLabSession::Create);
		GGLAB_ASSERT_MSG(postProcessRegistered, "Failed to register the Post Process Lab session.");

		const bool alphaTestRegistered = m_Runtime.RegisterLab(
			AlphaTestLabSession::GetDescriptor(),
			&AlphaTestLabSession::Create);
		GGLAB_ASSERT_MSG(alphaTestRegistered, "Failed to register the Alpha Test Lab session.");

		const bool mathFoundationRegistered = m_Runtime.RegisterLab(
			MathFoundationLabSession::GetDescriptor(),
			&MathFoundationLabSession::Create);
		GGLAB_ASSERT_MSG(mathFoundationRegistered,
			"Failed to register the Math Foundation Lab session.");

		const bool taskSystemRegistered = m_Runtime.RegisterLab(
			TaskSystemLabSession::GetDescriptor(),
			&TaskSystemLabSession::Create);
		GGLAB_ASSERT_MSG(taskSystemRegistered,
			"Failed to register the Task System Lab session.");

		const bool assetPublicationRegistered = m_Runtime.RegisterLab(
			AssetPublicationLabSession::GetDescriptor(),
			&AssetPublicationLabSession::Create);
		GGLAB_ASSERT_MSG(assetPublicationRegistered,
			"Failed to register the Asset Publication Lab session.");

		const bool assetResidencyRegistered = m_Runtime.RegisterLab(
			AssetResidencyLabSession::GetDescriptor(),
			&AssetResidencyLabSession::Create);
		GGLAB_ASSERT_MSG(assetResidencyRegistered,
			"Failed to register the Asset Residency Lab session.");

		const bool environmentAssetRegistered = m_Runtime.RegisterLab(
			EnvironmentAssetLabSession::GetDescriptor(),
			&EnvironmentAssetLabSession::Create);
		GGLAB_ASSERT_MSG(environmentAssetRegistered,
			"Failed to register the Environment Asset Lab session.");
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
