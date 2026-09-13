#pragma once

namespace gglab
{
	class DiagnosticsRuntime;
	class PipelineCache;
	class VulkanContext;

	void RegisterVulkanSnapshotProviders(DiagnosticsRuntime& runtime, VulkanContext& context,
		PipelineCache* pipelineCache) noexcept;
}
