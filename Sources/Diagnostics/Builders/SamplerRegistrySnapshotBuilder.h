#pragma once

namespace gglab
{
	class SamplerRegistry;
	struct SamplerRegistrySnapshot;

	void BuildSamplerRegistrySnapshot(
		const SamplerRegistry& registry,
		SamplerRegistrySnapshot& snapshot) noexcept;
}
