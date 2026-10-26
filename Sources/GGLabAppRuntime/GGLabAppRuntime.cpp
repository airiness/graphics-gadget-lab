#include "GGLabAppRuntime.h"
#include "Parameters/SessionParameterControl.h"
#include "Capture/FrameSequenceCoordinator.h"

#include "Demo/DemoManager.h"
#include "GGLabRuntime/Graphics/RenderHost.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace gglab
{
	void GGLabAppRuntime::CheckOwnerThread() const noexcept
	{
		if (m_OwnerThread != std::this_thread::get_id())
		{
			std::fputs("AppRuntime lifecycle requires the owner thread.\n", stderr);
			std::fflush(stderr);
			std::abort();
		}
	}

	AppRuntimeInitializeResult GGLabAppRuntime::Initialize(
		const GGLabAppRuntimeCreateInfo& createInfo) noexcept
	{
		CheckOwnerThread();
		if (m_RenderHost)
		{
			m_RenderHost->CheckServiceLifecycleAccess();
		}
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
		UpdateSequenceParameterEvents();
		if (m_ParameterControl && m_ParameterControl->HasPendingParameterBatches())
		{
			error = "Busy: finish the pending parameter batch before starting a sequence.";
			return 0;
		}
		// Sequence-owned event batches cannot be admitted beside another owner's retained record.
		if (m_ParameterControl && !request.m_Events.empty() &&
			m_ParameterControl->GetParameterBatchCount() != 0)
		{
			error = "Busy: consume the retained parameter batch result before starting a sequence with events.";
			return 0;
		}
		request.m_ParameterProvenance = true;
		const auto id = m_FrameSequence->Start(std::move(request), error);
		if (id)
		{
			m_ParameterControl->Enable();
		}
		return id;
	}

	FrameSequenceCancelVerdict GGLabAppRuntime::CancelFrameSequence() noexcept
	{
		const auto verdict = m_FrameSequence ? m_FrameSequence->Cancel() : FrameSequenceCancelVerdict::NotFound;
		UpdateSequenceParameterEvents();
		return verdict;
	}

	void GGLabAppRuntime::EnableParameterControl() noexcept
	{
		if (CanAcceptSessionCommands() && m_ParameterControl)
		{
			m_ParameterControl->Enable();
		}
	}

	bool GGLabAppRuntime::CanRequestParameters(std::string& error) const noexcept
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
		if (m_ParameterControl->GetParameterBatchCount() != 0)
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

	uint64_t GGLabAppRuntime::RequestParameters(const ParameterRequest& request, std::string& error) noexcept
	{
		const auto* expectedRun = request.m_Expected ?
			&request.m_Expected->m_RunIdentity :
			request.m_ExpectedDomain ?
				&request.m_ExpectedDomain->m_RunIdentity :
				nullptr;
		if (expectedRun)
		{
			if (m_RunIdentity.empty())
			{
				error = "RunIdentityUnavailable: this session has no run identity, so it rejects guarded requests.";
				return 0;
			}
			if (*expectedRun != m_RunIdentity)
			{
				error = "ExpectedRunChanged: the session runs a different process than the expected run; nothing changed.";
				return 0;
			}
		}
		if (!CanRequestParameters(error))
		{
			return 0;
		}
		return m_ParameterControl->RequestParameters(request, error);
	}

	std::optional<ParameterControlStamp> GGLabAppRuntime::GetDomainControlStamp(ParameterControlDomain domain) const
	{
		return m_ParameterControl ?
			m_ParameterControl->GetDomainControlStamp(domain, m_RunIdentity) :
			std::nullopt;
	}

	const ParameterBatchStatus* GGLabAppRuntime::GetExternalParameterBatchStatus() const noexcept
	{
		return m_ParameterControl ? m_ParameterControl->GetParameterBatchStatus() : nullptr;
	}

	HostViewControlStamp GGLabAppRuntime::GetHostViewControlStamp() const
	{
		return {
			.m_RunIdentity = m_RunIdentity,
			.m_ContentIdentity = m_ParameterControl ? m_ParameterControl->GetContentIdentity() : 0,
			.m_HostViewRevision = m_ParameterControl ? m_ParameterControl->GetHostViewRevision() : 0,
		};
	}

	void GGLabAppRuntime::UpdateSequenceParameterEvents() noexcept
	{
		const auto* sequence = m_FrameSequence ? m_FrameSequence->GetStatus() : nullptr;
		if (!sequence || !m_ParameterControl)
		{
			return;
		}
		const ParameterBatchOwner owner{ sequence->m_SequenceId };
		const auto publish = [&]()
			{
				const auto batches = m_ParameterControl->GetParameterBatchStatuses(owner);
				for (const auto& event : m_FrameSequence->GetStatus()->m_Events)
				{
					if (event.m_Batch && !event.m_Batch->IsTerminal() &&
						std::ranges::none_of(batches, [&](const auto& batch)
						{
							return batch.m_BatchId == event.m_Batch->m_BatchId;
						}))
					{
						m_FrameSequence->OnParameterEventFailed("The sequence parameter receipt was lost.");
					}
				}
				for (const auto& batch : batches)
				{
					m_FrameSequence->OnParameterEventBatch(batch);
					if (batch.IsTerminal())
					{
						GGLAB_UNUSED(m_ParameterControl->TakeParameterBatchResult(batch.m_BatchId, owner));
					}
				}
			};
		publish();
		if (!m_FrameSequence->IsActive() ||
			m_FrameSequence->GetStatus()->m_State == FrameSequenceState::Finishing)
		{
			// Consumption must occur within the submitted sequence range, even while
			// captures/timing drain. Preserve reached stamps when closing the owner.
			m_ParameterControl->FinishParameterBatches(owner);
			publish();
		}
	}

	std::optional<ParameterBatchStatus> GGLabAppRuntime::TakeParameterBatchResult(uint64_t id) noexcept
	{
		return m_ParameterControl ? m_ParameterControl->TakeParameterBatchResult(id) : std::nullopt;
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
				UpdateSequenceParameterEvents();
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
			UpdateSequenceParameterEvents();
		}
	}

	void GGLabAppRuntime::HandleHostEvent(const AppHostEvent& event) noexcept
	{
		CheckOwnerThread();
		if (m_RenderHost)
		{
			m_RenderHost->CheckServiceLifecycleAccess();
		}
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
				if (m_RenderHost)
				{
					m_RenderHost->RevokeRenderServices();
				}
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
