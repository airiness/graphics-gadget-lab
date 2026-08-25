#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "DevTools/DevToolsRuntime.h"
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITexture.h"
#include "Graphics/RenderPipeline/RenderPipelineOverlayExtensionBase.h"

#include <imgui.h>

#include <filesystem>
#include <string>

namespace gglab
{
	class DevelopGuiPlatformBackend;
	class DevelopGuiRenderBackend;
	class PlatformWindow;
	class RHIContext;
	class RHIGraphicsCommandContext;
	struct DevelopGuiContext;

	class DevelopGuiSystem final : public RenderPipelineOverlayExtensionBase
	{
	public:
		enum class State : uint8_t
		{
			Inactive,
			Active,
			FrameOpen,
		};

		struct CreateInfo
		{
			PlatformWindow* m_Window = nullptr;
			RHIContext* m_RHIContext = nullptr;
			std::filesystem::path m_SettingsPath;
		};

	public:
		DevelopGuiSystem() noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiSystem);
		~DevelopGuiSystem() override;

		[[nodiscard]] bool Initialize(const CreateInfo& createInfo) noexcept;
		void Finalize() noexcept;

		[[nodiscard]] bool BeginFrame() noexcept;
		void Draw(DevelopGuiContext& context) noexcept;
		void RenderDrawData(
			RHIGraphicsCommandContext* commandContext, RHITextureViewHandle renderTarget) noexcept;
		void AddOverlayPasses(RenderGraph& renderGraph, const RenderFrameContext& frameContext,
			const RenderServices& services) noexcept override;
		void EndFrame() noexcept;

		[[nodiscard]] State GetState() const noexcept { return m_State; }
		[[nodiscard]] bool IsActive() const noexcept { return m_State != State::Inactive; }
		[[nodiscard]] bool IsFrameOpen() const noexcept { return m_State == State::FrameOpen; }
		[[nodiscard]] bool WantsKeyboardCapture() const noexcept;
		[[nodiscard]] bool WantsMouseCapture() const noexcept;

		// The returned backend bridge ID is valid for the current open ImGui
		// frame. Callers resolve persistent RHI descriptors again each frame.
		[[nodiscard]] ImTextureID ResolveTextureId(RHIDescriptorHandle descriptor) const noexcept;

		[[nodiscard]] DevToolsRuntime& GetDevToolsRuntime() noexcept { return m_DevToolsRuntime; }
		[[nodiscard]] const DevToolsRuntime& GetDevToolsRuntime() const noexcept
		{
			return m_DevToolsRuntime;
		}

	private:
		std::unique_ptr<DevelopGuiPlatformBackend> m_PlatformBackend;
		std::unique_ptr<DevelopGuiRenderBackend> m_RenderBackend;
		DevToolsRuntime m_DevToolsRuntime;
		std::string m_SettingsPath;
		State m_State = State::Inactive;
	};
}
