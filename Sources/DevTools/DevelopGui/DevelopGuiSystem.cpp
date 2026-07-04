#include "Core/Precompiled.h"
#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "DevTools/DevelopGui/DevelopGuiBackendFactory.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiPanelCatalog.h"
#include "DevTools/DevelopGui/DevelopGuiPlatformBackend.h"
#include "DevTools/DevelopGui/DevelopGuiRenderBackend.h"

namespace gglab
{
	DevelopGuiSystem::DevelopGuiSystem() noexcept = default;

	DevelopGuiSystem::~DevelopGuiSystem()
	{
		GGLAB_ASSERT_MSG(m_State == State::Inactive,
			"DevelopGuiSystem destroyed without Finalize.");
	}

	bool DevelopGuiSystem::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (m_State != State::Inactive || !createInfo.m_Window || !createInfo.m_RHIContext)
		{
			return false;
		}

		IMGUI_CHECKVERSION();
		if (!ImGui::CreateContext())
		{
			GGLAB_LOG_GRAPHICS_ERROR("Failed to create the ImGui context.");
			return false;
		}

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		ImGui::StyleColorsDark();

		m_PlatformBackend = CreateDevelopGuiPlatformBackend(*createInfo.m_Window);
		m_RenderBackend = CreateDevelopGuiRenderBackend(*createInfo.m_RHIContext);
		if (!m_PlatformBackend || !m_RenderBackend ||
			!m_PlatformBackend->Initialize(*createInfo.m_Window) ||
			!m_RenderBackend->Initialize(*createInfo.m_RHIContext))
		{
			if (m_RenderBackend)
			{
				m_RenderBackend->Finalize();
			}
			if (m_PlatformBackend)
			{
				m_PlatformBackend->Finalize();
			}
			m_RenderBackend.reset();
			m_PlatformBackend.reset();
			ImGui::DestroyContext();
			return false;
		}

		devtools::RegisterDefaultDevelopGuiPanels(m_DevToolsRuntime.GetRegistry());
		m_State = State::Active;
		return true;
	}

	void DevelopGuiSystem::Finalize() noexcept
	{
		if (m_State == State::Inactive)
		{
			return;
		}

		EndFrame();
		m_DevToolsRuntime.Reset();

		if (m_RenderBackend)
		{
			m_RenderBackend->Finalize();
			m_RenderBackend.reset();
		}
		if (m_PlatformBackend)
		{
			m_PlatformBackend->Finalize();
			m_PlatformBackend.reset();
		}

		ImGui::DestroyContext();
		m_State = State::Inactive;
	}

	bool DevelopGuiSystem::BeginFrame() noexcept
	{
		GGLAB_ASSERT_MSG(m_State != State::FrameOpen,
			"DevelopGuiSystem::BeginFrame called twice without ending the previous frame.");
		if (m_State != State::Active || !m_PlatformBackend || !m_RenderBackend)
		{
			return false;
		}

		m_RenderBackend->NewFrame();
		m_PlatformBackend->NewFrame();
		ImGui::NewFrame();
		m_State = State::FrameOpen;
		return true;
	}

	void DevelopGuiSystem::Draw(DevelopGuiContext& context) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::FrameOpen,
			"DevelopGuiSystem::Draw called without BeginFrame.");
		if (m_State != State::FrameOpen)
		{
			return;
		}

		context.m_DevelopGuiSystem = this;
		m_DevToolsRuntime.Draw(context);
	}

	void DevelopGuiSystem::RenderDrawData(
		RHIGraphicsCommandContext* commandContext,
		RHITextureViewHandle renderTarget) noexcept
	{
		GGLAB_ASSERT_MSG(m_State == State::FrameOpen,
			"DevelopGuiSystem::RenderDrawData called without BeginFrame.");
		if (m_State != State::FrameOpen || !m_RenderBackend)
		{
			return;
		}

		ImGui::Render();
		m_State = State::Active;
		m_RenderBackend->RenderDrawData(commandContext, renderTarget);
	}

	void DevelopGuiSystem::EndFrame() noexcept
	{
		if (m_State != State::FrameOpen)
		{
			return;
		}

		ImGui::EndFrame();
		m_State = State::Active;
	}

	ImTextureID DevelopGuiSystem::ResolveTextureId(
		RHIDescriptorHandle descriptor) const noexcept
	{
		return IsActive() && m_RenderBackend ?
			m_RenderBackend->ResolveTextureId(descriptor) : ImTextureID{};
	}
}
