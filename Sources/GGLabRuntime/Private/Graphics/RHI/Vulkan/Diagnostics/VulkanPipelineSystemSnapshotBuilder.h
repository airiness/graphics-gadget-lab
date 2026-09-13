#pragma once

namespace gglab
{
	class PipelineCache;
	class VulkanPipelineSystem;
	struct RHIPipelineSystemSnapshot;

	void BuildVulkanPipelineSystemSnapshot(const VulkanPipelineSystem& system,
		const PipelineCache* pipelineCache, RHIPipelineSystemSnapshot& outSnapshot) noexcept;
}
