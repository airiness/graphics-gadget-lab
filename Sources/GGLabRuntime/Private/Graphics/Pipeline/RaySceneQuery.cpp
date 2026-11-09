#include "Graphics/Pipeline/RaySceneQuery.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <array>

namespace gglab::ray_scene_bindings
{
	namespace
	{
		[[nodiscard]] auto QueryBuffers(const RaySceneGraphResources& scene) noexcept
		{
			return std::array{ scene.m_Vertices, scene.m_Indices, scene.m_Geometries,
				scene.m_Instances, scene.m_Materials };
		}
	}

	void DeclareQueryReads(RenderGraph::RGBuilder& builder, const RaySceneGraphResources& scene,
		QueryInputs inputs) noexcept
	{
		GGLAB_UNUSED(builder.Read(scene.m_TopLevel, RGAccelerationStructureAccess::QueryRead));
		for (const auto blas : scene.m_BottomLevels)
		{
			GGLAB_UNUSED(builder.Read(blas, RGAccelerationStructureAccess::QueryRead));
		}
		const auto buffers = QueryBuffers(scene);
		const uint32_t count = inputs == QueryInputs::GeometryOnly ? GeometryOnlyCount : Count;
		for (uint32_t slot = Vertices; slot < count; ++slot)
		{
			GGLAB_UNUSED(builder.Read(buffers[slot - Vertices], RGBufferAccess::StructuredRead,
				RHIStage::ComputeShader));
		}
	}

	bool BindQueryResources(RHIComputeCommandContext& commands, const RGExecuteContext& execute,
		const RaySceneGraphResources& scene, std::span<const uint32_t> parameters) noexcept
	{
		if (parameters.size() != GeometryOnlyCount && parameters.size() != Count)
		{
			commands.RejectRecording();
			return false;
		}
		if (!commands.SetAccelerationStructure(parameters[TopLevel],
			execute.GetAccelerationStructureHandle(scene.m_TopLevel)))
		{
			return false;
		}
		const auto buffers = QueryBuffers(scene);
		for (uint32_t slot = Vertices; slot < parameters.size(); ++slot)
		{
			commands.SetReadOnlyBuffer(parameters[slot], execute.GetBufferHandle(buffers[slot - Vertices]));
			if (commands.HasRecordingError())
			{
				return false;
			}
		}
		return true;
	}
}
