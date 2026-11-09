#pragma once

#include "Graphics/Pipeline/RaySceneShaderBindings.h"
#include "GGLabRuntime/Graphics/RenderGraph/RenderGraph.h"

#include <cstdint>
#include <span>

namespace gglab
{
	class RHIComputeCommandContext;
	struct RaySceneGraphResources;
}

namespace gglab::ray_scene_bindings
{
	enum class QueryInputs : uint8_t
	{
		GeometryOnly,
		Materials,
	};

	// Query dependencies include every traversed BLAS, not only the bound TLAS.
	void DeclareQueryReads(RenderGraph::RGBuilder& builder, const RaySceneGraphResources& scene,
		QueryInputs inputs = QueryInputs::Materials) noexcept;

	// The caller retains scene.m_Lifetime on this encoder before query setup and owns
	// its pipeline/constants and any pass-specific bindings. This helper owns no resources.
	[[nodiscard]] bool BindQueryResources(RHIComputeCommandContext& commands,
		const RGExecuteContext& execute, const RaySceneGraphResources& scene,
		std::span<const uint32_t> parameters) noexcept;
}
