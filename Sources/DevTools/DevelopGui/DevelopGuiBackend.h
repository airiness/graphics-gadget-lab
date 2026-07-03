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
		struct CreateInfo
		{
			void* m_NativeWindowHandle = nullptr;
			RHIContext* m_RHIContext = nullptr;
		};

	public:
		DevelopGuiBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiBackend);
		virtual ~DevelopGuiBackend() = default;

		virtual void Initialize(const CreateInfo& createInfo) noexcept = 0;
		virtual void Finalize() noexcept = 0;

		virtual void NewFrame() noexcept = 0;
		virtual void RenderDrawData(
			RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept = 0;
		virtual void EndFrame() noexcept = 0;
		[[nodiscard]] virtual ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept = 0;
	};
}
