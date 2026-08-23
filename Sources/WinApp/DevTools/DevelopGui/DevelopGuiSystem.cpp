#include "DevTools/DevelopGui/DevelopGuiSystem.h"
#include "Core/Log/LogMacros.h"
#include "DevTools/DevelopGui/DevelopGuiBackendFactory.h"
#include "DevTools/DevelopGui/DevelopGuiContext.h"
#include "DevTools/DevelopGui/DevelopGuiPanelCatalog.h"
#include "DevTools/DevelopGui/DevelopGuiPlatformBackend.h"
#include "DevTools/DevelopGui/DevelopGuiRenderBackend.h"
#include "Graphics/RenderContexts.h"
#include "Graphics/RenderGraph/RenderGraph.h"
#include "Graphics/RenderPass/IBLGraphResources.h"
#include "Graphics/RenderPass/ShadowGraphResources.h"
#include "Graphics/RenderPipeline/RenderPipelineBlackboard.h"

#include <imgui.h>

namespace gglab
{
	namespace
	{
		struct DevelopGuiPassData
		{
			RGTextureId m_BackBuffer{};
			RGTextureId m_BrdfLut{};
			RGTextureId m_EnvironmentCubemapPreview{};
			RGTextureId m_IrradianceCubemapPreview{};
			RGTextureId m_PrefilteredSpecularCubemapPreview{};
			RGTextureId m_DirectionalShadowMapPreview{};
			RGTextureViewId m_Rtv{};
		};
	}

	DevelopGuiSystem::DevelopGuiSystem() noexcept = default;

	DevelopGuiSystem::~DevelopGuiSystem()
	{
		GGLAB_ASSERT_MSG(
			m_State == State::Inactive, "DevelopGuiSystem destroyed without Finalize.");
	}

	bool DevelopGuiSystem::Initialize(const CreateInfo& createInfo) noexcept
	{
		if (m_State != State::Inactive || !createInfo.m_Window || !createInfo.m_RHIContext ||
			createInfo.m_SettingsPath.empty() || !createInfo.m_SettingsPath.is_absolute())
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
		m_SettingsPath = createInfo.m_SettingsPath.string();
		io.IniFilename = m_SettingsPath.c_str();
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
			m_SettingsPath.clear();
			return false;
		}

		devtools::RegisterDefaultDevelopGuiPanels(
			m_DevToolsRuntime.GetRegistry(), *createInfo.m_RHIContext);
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
		m_SettingsPath.clear();
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

		if (!m_RenderBackend->NewFrame())
		{
			GGLAB_LOG_GRAPHICS_ERROR(
				"DevelopGui render backend could not prepare the current frame.");
			return false;
		}
		m_PlatformBackend->NewFrame();
		ImGui::NewFrame();
		m_State = State::FrameOpen;
		return true;
	}

	void DevelopGuiSystem::Draw(DevelopGuiContext& context) noexcept
	{
		GGLAB_ASSERT_MSG(
			m_State == State::FrameOpen, "DevelopGuiSystem::Draw called without BeginFrame.");
		if (m_State != State::FrameOpen)
		{
			return;
		}

		context.m_DevelopGuiSystem = this;
		m_DevToolsRuntime.Draw(context);
	}

	void DevelopGuiSystem::RenderDrawData(
		RHIGraphicsCommandContext* commandContext, RHITextureViewHandle renderTarget) noexcept
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

	void DevelopGuiSystem::AddOverlayPasses(RenderGraph& renderGraph,
		const RenderFrameContext& frameContext, const RenderServices&) noexcept
	{
		if (!IsFrameOpen())
		{
			return;
		}
		const RenderViewID displayViewId = frameContext.GetDisplayViewId();

		renderGraph.AddPass<DevelopGuiPassData>("UI.DevelopGui",
			[displayViewId](RenderGraph::RGBuilder& builder, DevelopGuiPassData& data)
			{
				builder.SideEffect();

				auto& blackboard = builder.GetBlackboard();
				auto& targetsTable =
					blackboard.GetOrCreate<RGViewTargetsTable>(ViewTargetsTableName);
				auto& viewTargets = targetsTable.GetViewTargets(displayViewId);
				builder.ReadWriteInPlace(viewTargets.m_BackBuffer, RGTextureAccess::RenderTarget);
				data.m_BackBuffer = viewTargets.m_BackBuffer;
				data.m_Rtv = builder.CreateView<RHITextureViewType::RenderTarget>(data.m_BackBuffer);

				if (const auto* iblRes = blackboard.TryGet<RGIBLResources>(IBLResourcesName);
					iblRes && iblRes->m_BrdfLut.IsValid())
				{
					data.m_BrdfLut = builder.Read(iblRes->m_BrdfLut, RGTextureAccess::Sample);
				}

				if (const auto* iblPreviewRes =
					blackboard.TryGet<RGIBLPreviewResources>(IBLPreviewResourcesName))
				{
					if (iblPreviewRes->m_EnvironmentCubemapPreview.IsValid())
					{
						data.m_EnvironmentCubemapPreview = builder.Read(
							iblPreviewRes->m_EnvironmentCubemapPreview, RGTextureAccess::Sample);
					}
					if (iblPreviewRes->m_IrradianceCubemapPreview.IsValid())
					{
						data.m_IrradianceCubemapPreview = builder.Read(
							iblPreviewRes->m_IrradianceCubemapPreview, RGTextureAccess::Sample);
					}
					if (iblPreviewRes->m_PrefilteredSpecularCubemapPreview.IsValid())
					{
						data.m_PrefilteredSpecularCubemapPreview = builder.Read(
							iblPreviewRes->m_PrefilteredSpecularCubemapPreview,
							RGTextureAccess::Sample);
					}
				}

				if (const auto* shadowRes = blackboard.TryGet<RGShadowResources>(ShadowResourcesName);
					shadowRes && shadowRes->m_DirectionalShadowMapPreview.IsValid())
				{
					data.m_DirectionalShadowMapPreview = builder.Read(
						shadowRes->m_DirectionalShadowMapPreview, RGTextureAccess::Sample);
				}
			},
			[this](RGExecuteContext& executeContext, DevelopGuiPassData& data)
			{
				GGLAB_UNUSED(data.m_BrdfLut);
				GGLAB_UNUSED(data.m_EnvironmentCubemapPreview);
				GGLAB_UNUSED(data.m_IrradianceCubemapPreview);
				GGLAB_UNUSED(data.m_PrefilteredSpecularCubemapPreview);
				GGLAB_UNUSED(data.m_DirectionalShadowMapPreview);

				if (!IsFrameOpen())
				{
					return;
				}

				RenderDrawData(executeContext.GetGraphicsCommandContext(),
					executeContext.GetViewHandle(data.m_Rtv));
			});
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

	bool DevelopGuiSystem::WantsKeyboardCapture() const noexcept
	{
		return IsActive() && ImGui::GetIO().WantCaptureKeyboard;
	}

	bool DevelopGuiSystem::WantsMouseCapture() const noexcept
	{
		return IsActive() && ImGui::GetIO().WantCaptureMouse;
	}

	ImTextureID DevelopGuiSystem::ResolveTextureId(RHIDescriptorHandle descriptor) const noexcept
	{
		return IsFrameOpen() && m_RenderBackend ? m_RenderBackend->ResolveTextureId(descriptor)
			: ImTextureID{};
	}
}
