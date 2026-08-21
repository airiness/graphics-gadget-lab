#pragma once

namespace gglab
{
	class DX12PipelineSystem;
	class PipelineCache;
	class VulkanPipelineSystem;
	struct RHIPipelineSystemSnapshot;

	void BuildDX12PipelineSystemSnapshot(const DX12PipelineSystem& system,
		const PipelineCache* pipelineCache, RHIPipelineSystemSnapshot& outSnapshot) noexcept;
	void BuildVulkanPipelineSystemSnapshot(const VulkanPipelineSystem& system,
		const PipelineCache* pipelineCache, RHIPipelineSystemSnapshot& outSnapshot) noexcept;
}
