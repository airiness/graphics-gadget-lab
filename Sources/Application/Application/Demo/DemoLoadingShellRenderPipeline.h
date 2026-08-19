#pragma once

#include <memory>

namespace gglab
{
	class RenderPipelineBase;

	[[nodiscard]] std::unique_ptr<RenderPipelineBase>
		CreateDemoLoadingShellRenderPipeline(bool minimalGraphicsSmoke = false) noexcept;
}
