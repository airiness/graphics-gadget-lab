#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIDescriptor.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"

#include <imgui.h>

namespace gglab
{
	class RHIContext;
	class RHIGraphicsCommandContext;

	class DevelopGuiRenderBackend
	{
	public:
		DevelopGuiRenderBackend() noexcept = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(DevelopGuiRenderBackend);
		virtual ~DevelopGuiRenderBackend() = default;

		[[nodiscard]] virtual bool Initialize(RHIContext& context) noexcept = 0;
		virtual void Finalize() noexcept = 0;
		[[nodiscard]] virtual bool NewFrame() noexcept = 0;
		virtual void RenderDrawData(RHIGraphicsCommandContext* commandContext,
			RHITextureViewHandle renderTarget) noexcept = 0;
		[[nodiscard]] virtual ImTextureID ResolveTextureId(
			RHIDescriptorHandle descriptor) const noexcept = 0;
	};
}
