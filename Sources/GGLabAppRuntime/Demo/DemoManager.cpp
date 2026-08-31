#include "Demo/DemoManager.h"
#include "AppRuntimeLog.h"
#include "Core/Log/LogMacros.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/RHIDevice.h"

namespace gglab
{
	DemoManager::DemoManager(Renderer* renderer) noexcept : m_Renderer(renderer)
	{
		GGLAB_ASSERT_NOT_NULL(m_Renderer);
	}

	DemoManager::~DemoManager()
	{
		if (!m_IsPreparedForAssetShutdown && m_ActiveDemo)
		{
			m_ActiveDemo->OnExit();
		}
		if (!m_IsPreparedForAssetShutdown && m_PendingDemo)
		{
			m_PendingDemo->CancelPrepare();
		}
	}

	void DemoManager::PrepareForAssetShutdown() noexcept
	{
		if (m_IsPreparedForAssetShutdown)
		{
			return;
		}
		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnExit();
		}
		if (m_PendingDemo)
		{
			m_PendingDemo->CancelPrepare();
		}
		m_IsPreparedForAssetShutdown = true;
	}

	DemoBase* DemoManager::GetDemo(uint32_t index) const noexcept
	{
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: GetDemo, invalid index:{}, size:{}.", index, m_DemoSlots.size());

			return nullptr;
		}

		if (index == m_ActiveDemoIndex)
		{
			return m_ActiveInstance.get();
		}
		if (index == m_PendingDemoIndex)
		{
			return m_PendingDemo.get();
		}
		const auto iterator = std::ranges::find_if(m_RetiringDemos,
			[index](const RetiringDemo& retiring) noexcept { return retiring.m_Index == index; });
		return iterator != m_RetiringDemos.end() ? iterator->m_Instance.get() : nullptr;
	}

	std::string_view DemoManager::GetDemoName(uint32_t index) const noexcept
	{
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN(
				"DemoManager: GetDemoName, invalid index:{}, size:{}.", index, m_DemoSlots.size());
			return {};
		}
		return m_DemoSlots[index].m_Name;
	}

	bool DemoManager::IsDemoCreated(uint32_t index) const noexcept
	{
		return GetDemo(index) != nullptr;
	}

	uint32_t DemoManager::RegisterDemo(std::string name, DemoFactory factory) noexcept
	{
		if (name.empty() || !factory)
		{
			GGLAB_LOG_GRAPHICS_ERROR("DemoManager: cannot register an invalid demo slot.");
			return InvalidDemoIndex;
		}
		if (std::ranges::any_of(m_DemoSlots,
			[&name](const DemoSlot& slot) noexcept { return slot.m_Name == name; }))
		{
			GGLAB_LOG_GRAPHICS_ERROR("DemoManager: demo '{}' is already registered.", name);
			return InvalidDemoIndex;
		}

		const uint32_t registeredIndex = static_cast<uint32_t>(m_DemoSlots.size());
		m_DemoSlots.push_back({
			.m_Name = std::move(name),
			.m_Factory = std::move(factory),
			});
		return registeredIndex;
	}

	void DemoManager::SetBootstrapDemo(std::unique_ptr<DemoBase> demo) noexcept
	{
		GGLAB_ASSERT_MSG(!m_ActiveInstance, "DemoManager bootstrap demo can only be set once.");
		GGLAB_ASSERT_NOT_NULL(demo.get());
		if (m_ActiveInstance || !demo)
		{
			return;
		}

		m_ActiveInstance = std::move(demo);
		m_ActiveDemo = m_ActiveInstance.get();
		m_ActiveDemoIndex = InvalidDemoIndex;
		++m_TemporalSessionSerial;
		GGLAB_ASSERT_MSG(m_TemporalSessionSerial != 0,
			"Demo temporal session serial overflowed its valid range.");
		m_ActiveDemo->OnEnter();
		if (m_WindowWidth > 0 && m_WindowHeight > 0)
		{
			m_ActiveDemo->OnResize(m_WindowWidth, m_WindowHeight);
		}
	}

	void DemoManager::RequestActiveDemo(uint32_t index) noexcept
	{
		if (index >= m_DemoSlots.size())
		{
			GGLAB_LOG_GRAPHICS_WARN("DemoManager: RequestActiveDemo, invalid index:{}, size:{}.",
				index, m_DemoSlots.size());

			return;
		}
		m_RequestedDemoIndex = index;
	}

	void DemoManager::BeginTransitionTick() noexcept
	{
		PollRetiringDemos();
		BeginRequestedTransition();
	}

	bool DemoManager::CompleteTransitionTick() noexcept
	{
		if (!m_PendingDemo)
		{
			return m_ActiveDemo != nullptr;
		}

		m_PendingDemo->TickPrepare();
		const LoadingProgress progress = m_PendingDemo->GetPreparationProgress();
		if (progress.HasFailed())
		{
			GGLAB_LOG_ERROR("DemoManager: failed to prepare demo '{}': {}",
				GetDemoName(m_PendingDemoIndex), progress.m_Detail);
			m_PendingDemo->CancelPrepare();
			m_PendingDemo.reset();
			m_PendingDemoIndex = InvalidDemoIndex;
			return m_ActiveDemo != nullptr;
		}
		if (progress.IsReady())
		{
			return CommitPendingDemo();
		}
		return m_ActiveDemo != nullptr;
	}

	bool DemoManager::TickTransitions() noexcept
	{
		BeginTransitionTick();
		return CompleteTransitionTick();
	}

	void DemoManager::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		m_LastActiveFrame = feedback;
		m_HasLastActiveFrame = true;
		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnFrameSubmitted(feedback);
		}
	}

	std::optional<LoadingProgress> DemoManager::GetLoadingProgress() const noexcept
	{
		if (m_PendingDemo)
		{
			LoadingProgress progress = m_PendingDemo->GetPreparationProgress();
			const std::string nestedTitle = std::move(progress.m_Title);
			progress.m_Title =
				nestedTitle.empty()
				? std::format("Loading Demo: {}", GetDemoName(m_PendingDemoIndex))
				: std::format(
					"Loading Demo: {} / {}", GetDemoName(m_PendingDemoIndex), nestedTitle);
			return progress;
		}
		if (m_RequestedDemoIndex != InvalidDemoIndex)
		{
			return LoadingProgress{
				.m_Status = LoadingStatus::Preparing,
				.m_Fraction = 0.0f,
				.m_Title = std::format("Loading Demo: {}", GetDemoName(m_RequestedDemoIndex)),
				.m_Stage = "Creating demo",
				.m_Detail = "Waiting for the next frame boundary.",
			};
		}
		return m_ActiveDemo ? m_ActiveDemo->GetActiveLoadingProgress() : std::nullopt;
	}

	uint32_t DemoManager::GetPendingActiveIndex() const noexcept
	{
		return m_RequestedDemoIndex != InvalidDemoIndex ? m_RequestedDemoIndex : m_PendingDemoIndex;
	}

	void DemoManager::BeginRequestedTransition() noexcept
	{
		if (m_RequestedDemoIndex == InvalidDemoIndex)
		{
			return;
		}

		const uint32_t requestedIndex = std::exchange(m_RequestedDemoIndex, InvalidDemoIndex);
		if (requestedIndex == m_ActiveDemoIndex)
		{
			if (m_PendingDemo)
			{
				m_PendingDemo->CancelPrepare();
				m_PendingDemo.reset();
				m_PendingDemoIndex = InvalidDemoIndex;
			}
			return;
		}
		if (requestedIndex == m_PendingDemoIndex)
		{
			return;
		}

		if (m_PendingDemo)
		{
			m_PendingDemo->CancelPrepare();
			m_PendingDemo.reset();
		}
		m_PendingDemoIndex = requestedIndex;
		m_PendingDemo = m_DemoSlots[requestedIndex].m_Factory();
		if (!m_PendingDemo)
		{
			GGLAB_LOG_ERROR(
				"DemoManager: failed to create demo '{}'.", GetDemoName(requestedIndex));
			m_PendingDemoIndex = InvalidDemoIndex;
			return;
		}

		m_PendingDemo->OnResize(m_WindowWidth, m_WindowHeight);
		m_PendingDemo->BeginPrepare();
	}

	bool DemoManager::CommitPendingDemo() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_PendingDemo.get());
		m_PendingDemo->CommitPrepare();
		if (m_ActiveInstance)
		{
			m_ActiveInstance->OnExit();
			m_RetiringDemos.push_back({
				.m_Index = m_ActiveDemoIndex,
				.m_Instance = std::move(m_ActiveInstance),
				.m_RetireFence =
					m_HasLastActiveFrame ? m_LastActiveFrame.m_SubmittedFence : RHIFencePoint{},
				});
		}

		m_ActiveInstance = std::move(m_PendingDemo);
		m_ActiveDemo = m_ActiveInstance.get();
		m_ActiveDemoIndex = std::exchange(m_PendingDemoIndex, InvalidDemoIndex);
		++m_TemporalSessionSerial;
		GGLAB_ASSERT_MSG(m_TemporalSessionSerial != 0,
			"Demo temporal session serial overflowed its valid range.");
		m_LastActiveFrame = {};
		m_HasLastActiveFrame = false;
		m_ActiveDemo->OnEnter();
		m_ActiveDemo->OnResize(m_WindowWidth, m_WindowHeight);
		GGLAB_LOG_INFO("Activated demo '{}'.", GetDemoName(m_ActiveDemoIndex));
		return true;
	}

	void DemoManager::PollRetiringDemos() noexcept
	{
		RHIDevice* device = m_Renderer ? m_Renderer->GetDevice() : nullptr;
		if (!device)
		{
			return;
		}
		std::erase_if(m_RetiringDemos,
			[device](const RetiringDemo& retiring) noexcept
			{
				return !retiring.m_RetireFence.IsValid() ||
					device->IsFencePointCompleted(retiring.m_RetireFence);
			});
	}

	void DemoManager::OnResize(uint32_t width, uint32_t height) noexcept
	{
		m_WindowWidth = width;
		m_WindowHeight = height;
		if (m_ActiveDemo)
		{
			m_ActiveDemo->OnResize(width, height);
		}
		if (m_PendingDemo)
		{
			m_PendingDemo->OnResize(width, height);
		}
	}
}
