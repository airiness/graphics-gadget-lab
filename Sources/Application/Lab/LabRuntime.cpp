#include "Core/Precompiled.h"
#include "Application/Lab/LabRuntime.h"
#include "Graphics/Renderer.h"
#include "Graphics/RHI/RHIContext.h"

namespace gglab
{
	LabRuntime::LabRuntime(const LabSessionCreateInfo& createInfo) noexcept :
		m_CreateInfo(createInfo)
	{
		GGLAB_ASSERT_MSG(createInfo.IsValid(), "LabRuntime requires valid create info.");
	}

	LabRuntime::~LabRuntime()
	{
		Shutdown();
	}

	bool LabRuntime::RegisterLab(LabDescriptor descriptor, LabSessionFactory factory) noexcept
	{
		if (m_State != LabRuntimeState::Uninitialized)
		{
			GGLAB_LOG_ERROR("Labs must be registered before LabRuntime initialization.");
			return false;
		}
		return m_Catalog.Register(std::move(descriptor), factory);
	}

	bool LabRuntime::Initialize(const LabId& startupLab) noexcept
	{
		if (m_State != LabRuntimeState::Uninitialized)
		{
			return IsReady();
		}

		if (!m_CreateInfo.IsValid())
		{
			SetError("LabRuntime create info is invalid.");
			return false;
		}

		return ReplaceActiveSession(startupLab, false);
	}

	void LabRuntime::Shutdown() noexcept
	{
		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
		}
		m_IsEntered = false;
		m_ActiveSession.reset();
		m_State = LabRuntimeState::Uninitialized;
	}

	void LabRuntime::OnEnter() noexcept
	{
		if (!m_IsEntered && m_ActiveSession)
		{
			m_IsEntered = true;
			m_ActiveSession->OnEnter();
		}
	}

	void LabRuntime::OnExit() noexcept
	{
		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
			m_IsEntered = false;
		}
	}

	void LabRuntime::OnResize(uint32_t width, uint32_t height) noexcept
	{
		if (width == 0 || height == 0)
		{
			return;
		}

		m_CreateInfo.m_WindowWidth = width;
		m_CreateInfo.m_WindowHeight = height;
		if (m_ActiveSession)
		{
			m_ActiveSession->OnResize(width, height);
		}
	}

	void LabRuntime::Update() noexcept
	{
		GGLAB_ASSERT_MSG(IsReady(), "LabRuntime requires an active session before update.");
		if (m_ActiveSession)
		{
			m_ActiveSession->Update();
		}
	}

	void LabRuntime::OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept
	{
		if (m_ActiveSession)
		{
			m_ActiveSession->OnFrameSubmitted(feedback);
		}
	}

	void LabRuntime::ProcessPendingCommands() noexcept
	{
		const LabCommandBatch commands = m_CommandQueue.Consume();
		if (commands.m_SwitchTarget)
		{
			GGLAB_UNUSED(ReplaceActiveSession(*commands.m_SwitchTarget, true));
			return;
		}

		if (commands.m_RestartRequested && m_ActiveSession)
		{
			const LabId activeId = m_ActiveSession->GetDescriptor().m_Id;
			GGLAB_UNUSED(ReplaceActiveSession(activeId, true));
		}
	}

	World& LabRuntime::GetWorld() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetWorld();
	}

	Camera& LabRuntime::GetCamera() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetCamera();
	}

	CameraController& LabRuntime::GetCameraController() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetCameraController();
	}

	RenderPipelineBase& LabRuntime::GetRenderPipeline() noexcept
	{
		GGLAB_ASSERT_NOT_NULL(m_ActiveSession.get());
		return m_ActiveSession->GetRenderPipeline();
	}

	bool LabRuntime::ReplaceActiveSession(const LabId& id, bool waitForGpu) noexcept
	{
		if (!m_Catalog.Find(id))
		{
			SetError(std::format("Lab '{}' is not registered.", id.GetName()));
			return false;
		}

		auto nextSession = m_Catalog.Create(id, m_CreateInfo);
		if (!nextSession || !nextSession->IsValid())
		{
			SetError(std::format("Failed to create lab '{}'.", id.GetName()));
			return false;
		}

		if (waitForGpu && m_ActiveSession)
		{
			auto* renderer = m_CreateInfo.m_Services.m_Renderer;
			GGLAB_ASSERT_NOT_NULL(renderer);
			GGLAB_ASSERT_NOT_NULL(renderer->GetRHIContext());
			renderer->GetRHIContext()->WaitIdle();
		}

		if (m_IsEntered && m_ActiveSession)
		{
			m_ActiveSession->OnExit();
		}

		m_ActiveSession = std::move(nextSession);
		m_ActiveSession->OnResize(m_CreateInfo.m_WindowWidth, m_CreateInfo.m_WindowHeight);
		if (m_IsEntered)
		{
			m_ActiveSession->OnEnter();
		}

		m_LastError.clear();
		m_State = LabRuntimeState::Ready;
		GGLAB_LOG_INFO("Activated lab '{}'.", id.GetName());
		return true;
	}

	void LabRuntime::SetError(std::string message) noexcept
	{
		m_LastError = std::move(message);
		if (!m_ActiveSession)
		{
			m_State = LabRuntimeState::Failed;
		}
		GGLAB_LOG_ERROR("{}", m_LastError);
	}
}
