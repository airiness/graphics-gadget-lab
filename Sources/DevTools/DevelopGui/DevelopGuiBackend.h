#pragma once
#include "Graphics/RHI/RHIDescriptor.h"
#include "Graphics/RHI/RHITexture.h"

#include <imgui.h>

namespace gglab
{
	class RHIContext;
	class RHIGraphicsCommandContext;

	class DevelopGuiBackend
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
			void* m_NativeWindowHandle = nullptr;
			RHIContext* m_RHIContext = nullptr;
		};

	public:
		DevelopGuiBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiBackend);
		virtual ~DevelopGuiBackend() = default;

		[[nodiscard]] virtual bool Initialize(const CreateInfo& createInfo) noexcept = 0;
		virtual void Finalize() noexcept = 0;

		[[nodiscard]] virtual bool NewFrame() noexcept = 0;
		virtual void RenderDrawData(
			RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept = 0;
		virtual void EndFrame() noexcept = 0;

		[[nodiscard]] virtual State GetState() const noexcept = 0;
		[[nodiscard]] bool IsActive() const noexcept { return GetState() != State::Inactive; }
		[[nodiscard]] bool IsFrameOpen() const noexcept { return GetState() == State::FrameOpen; }

		[[nodiscard]] virtual ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept = 0;
	};
}
