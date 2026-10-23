#include "GGLabAppRuntime.h"
#include "Parameters/SessionParameterControl.h"
#include "Capture/FrameSequenceCoordinator.h"

#include "Demo/DemoManager.h"
#include "GGLabRuntime/Graphics/RenderHost.h"

#include <chrono>
#include <utility>

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
		m_CaptureJobLimits = createInfo.m_CaptureJobLimits;
		m_HostEnvironment = createInfo.m_HostEnvironment;
		m_StartedAt = std::chrono::system_clock::now();

		m_LifecycleState = AppRuntimeLifecycleState::Running;
		return AppRuntimeInitializeResult::Succeeded;
	}

	bool GGLabAppRuntime::CanAcceptSessionCommands() const noexcept
	{
		return m_ServicesInitialized &&
			(m_LifecycleState == AppRuntimeLifecycleState::Running ||
				m_LifecycleState == AppRuntimeLifecycleState::Suspended);
	}

	uint64_t GGLabAppRuntime::StartFrameSequence(
		FrameSequenceRequest request, std::string& error) noexcept
	{
		error.clear();
		if (!CanAcceptSessionCommands() || !m_FrameSequence)
		{
			error = "Frame sequences are unavailable in this session.";
			return 0;
		}
		const auto* batch = m_ParameterControl ? m_ParameterControl->GetParameterBatchStatus() : nullptr;
		if (batch && (!batch->IsTerminal() || !request.m_Events.empty()))
		{
			error = "Busy: finish the pending parameter batch before starting a sequence.";
			return 0;
		}
		const bool hasEvents = !request.m_Events.empty();
		const auto id = m_FrameSequence->Start(std::move(request), error);
		if (id && hasEvents)
		{
			m_ParameterControl->Enable();
		}
		return id;
	}

	FrameSequenceCancelVerdict GGLabAppRuntime::CancelFrameSequence() noexcept
	{
		const auto verdict = m_FrameSequence ? m_FrameSequence->Cancel() : FrameSequenceCancelVerdict::NotFound;
		UpdateSequenceParameterEvent();
		return verdict;
	}

	void GGLabAppRuntime::EnableParameterControl() noexcept
	{
		if (CanAcceptSessionCommands() && m_ParameterControl)
		{
			m_ParameterControl->Enable();
		}
	}

	bool GGLabAppRuntime::CanRequestParameterBatch(std::string& error) const noexcept
	{
		error.clear();
		if (!CanAcceptSessionCommands() || !m_ParameterControl)
		{
			error = "NoParameterSource: ready active content in a live runtime is required.";
			return false;
		}
		if (m_FrameSequence && m_FrameSequence->IsActive())
		{
			error = "Busy: session parameter changes are unavailable during a sequence.";
			return false;
		}
		if (m_ParameterControl->GetParameterBatchStatus())
		{
			error = "Busy: consume the previous parameter batch result first.";
			return false;
		}
		if (!m_ParameterControl->IsAvailable())
		{
			error = "NoParameterSource: ready active content is required.";
			return false;
		}
		return true;
	}

	uint64_t GGLabAppRuntime::RequestParameterBatch(
		std::span<const LabParameterValue> values, std::string& error) noexcept
	{
		if (!CanRequestParameterBatch(error))
		{
			return 0;
		}
		return m_ParameterControl->RequestParameterBatch(values, error);
	}

	const ParameterBatchStatus* GGLabAppRuntime::GetExternalParameterBatchStatus() const noexcept
	{
		const auto* batch = m_ParameterControl ? m_ParameterControl->GetParameterBatchStatus() : nullptr;
		return batch && batch->m_BatchId != m_SequenceParameterBatch ? batch : nullptr;
	}

	void GGLabAppRuntime::UpdateSequenceParameterEvent() noexcept
	{
		if (!m_SequenceParameterBatch || !m_ParameterControl || !m_FrameSequence)
		{
			return;
		}
		if (!m_FrameSequence->IsActive())
		{
			m_ParameterControl->FinishParameterBatch(m_SequenceParameterBatch);
		}
		const auto* batch = m_ParameterControl->GetParameterBatchStatus();
		if (!batch || batch->m_BatchId != m_SequenceParameterBatch)
		{
			m_FrameSequence->OnParameterEventFailed("The sequence parameter receipt was lost.");
			m_SequenceParameterBatch = 0;
			return;
		}
		m_FrameSequence->OnParameterEventBatch(*batch);
		if (batch->IsTerminal())
		{
			GGLAB_UNUSED(m_ParameterControl->TakeParameterBatchResult(m_SequenceParameterBatch));
			m_SequenceParameterBatch = 0;
		}
	}

	std::optional<ParameterBatchStatus> GGLabAppRuntime::TakeParameterBatchResult(uint64_t id) noexcept
	{
		return m_ParameterControl && id != m_SequenceParameterBatch ?
			m_ParameterControl->TakeParameterBatchResult(id) : std::nullopt;
	}

	void GGLabAppRuntime::SynchronizeContentBinding() noexcept
	{
		if (m_ParameterControl && m_DemoManager)
		{
			m_ActiveContentIdentity = m_DemoManager->GetActiveContentIdentity();
			const uint32_t index = m_DemoManager->GetActiveIndex();
			const std::string_view demoId = index < m_DemoManager->GetDemoCount() ?
				m_DemoManager->GetDemoName(index) :
				std::string_view{};
			m_ParameterControl->SynchronizeContent(m_DemoManager->GetActiveDemo(),
				m_ActiveContentIdentity, demoId, m_DemoManager->HasPendingActiveDemo());
			if (m_FrameSequence)
			{
				m_FrameSequence->OnContentBindingChanged(m_ActiveContentIdentity);
				UpdateSequenceParameterEvent();
			}
		}
	}

	void GGLabAppRuntime::ReleaseContentBinding() noexcept
	{
		if (m_ParameterControl)
		{
			m_ParameterControl->SynchronizeContent(nullptr, 0, {}, true);
		}
		m_ActiveContentIdentity = 0;
		if (m_FrameSequence)
		{
			m_FrameSequence->OnContentBindingChanged(0);
			UpdateSequenceParameterEvent();
		}
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
				if (m_RenderHost)
				{
					m_RenderHost->OnSuspend();
				}
				m_LifecycleState = AppRuntimeLifecycleState::Suspended;
			}
			break;
		case AppHostEventType::Resumed:
			if (m_LifecycleState == AppRuntimeLifecycleState::Suspended)
			{
				if (m_RenderHost)
				{
					m_RenderHost->OnResume();
				}
				if (m_ResizePending)
				{
					m_RenderHost->OnResize(m_WindowWidth, m_WindowHeight);
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

		m_RenderHost->OnResize(width, height);
		m_DemoManager->OnResize(width, height);
		m_ResizePending = false;
	}
}
