#pragma once
#include "GGLabRuntime/Graphics/RenderContexts.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"
#include "GGLabRuntime/Graphics/RenderServices.h"

#include <memory>

namespace gglab
{
	// Narrow authoring contract for the IBL resource-setup pass family. The
	// concrete pass roster stays Runtime-internal; host-selected content such as
	// the AppRuntime loading shell composes it through this interface.
	class IBLGraphSetupPass
	{
	public:
		virtual ~IBLGraphSetupPass() = default;

		virtual void AddPass(RenderGraph& rg, const RenderFrameContext& context,
			const RenderServices& services) noexcept = 0;
		virtual void AddFinishPass(RenderGraph& rg) noexcept = 0;
	};

	[[nodiscard]] std::unique_ptr<IBLGraphSetupPass> CreateIBLGraphSetupPass() noexcept;
}
