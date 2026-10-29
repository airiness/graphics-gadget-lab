#pragma once
#include "GGLabRuntime/Graphics/GPUStructures.h"
#include "GGLabRuntime/Graphics/GraphicsHandles.h"
#include "GGLabRuntime/Graphics/MaterialTypes.h"
#include "GGLabRuntime/Graphics/RayScene.h"
#include "GGLabRuntime/Graphics/RHI/RHIAccelerationStructure.h"
#include "GGLabRuntime/Graphics/RHI/RHICommandContext.h"

#include <array>
#include <span>
#include <vector>

namespace gglab
{
	struct RaySceneGeometryKey
	{
		MeshID m_Mesh{};
		uint64_t m_ContentGeneration = 0;
		bool operator==(const RaySceneGeometryKey&) const noexcept = default;
	};

	struct RaySceneSource
	{
		uint32_t m_EntityIdentity = 0;
		ModelID m_Model{};
		uint64_t m_ModelGeneration = 0;
		uint32_t m_ModelMeshIndex = 0;
		RaySceneGeometryKey m_Geometry;
		RHIVertexBufferBinding m_Vertices;
		RHIIndexBufferBinding m_Indices;
		uint32_t m_VertexCount = 0;
		uint32_t m_IndexCount = 0;
		RenderMaterialKey m_MaterialKey;
		MaterialGPU m_Material{};
		AlphaMode m_AlphaMode = AlphaMode::Opaque;
		std::array<float, 12> m_Transform{};
	};

	struct RayScenePlannedGeometry
	{
		RaySceneGeometryKey m_Key;
		RHIVertexBufferBinding m_SourceVertices;
		RHIIndexBufferBinding m_SourceIndices;
		RaySceneGeometryGPU m_GPU;
	};

	struct RayScenePlan
	{
		std::vector<RaySceneSource> m_Sources;
		std::vector<RayScenePlannedGeometry> m_Geometries;
		std::vector<RaySceneInstanceGPU> m_Instances;
		std::vector<MaterialGPU> m_Materials;
		uint64_t m_VertexBytes = 0;
		uint64_t m_IndexBytes = 0;
		uint32_t m_OpaqueCount = 0;
		uint32_t m_BlendCount = 0;
	};

	// Pure preparation used by the production World collector and contract tests.
	[[nodiscard]] RaySceneStatus PlanRayScene(std::vector<RaySceneSource> sources,
		const RaySceneCapacity& capacity, const RHIASBuildLimits& limits,
		RayScenePlan& result, std::string& reason) noexcept;
	[[nodiscard]] bool HasSameRayGeometry(const RayScenePlan& a, const RayScenePlan& b) noexcept;
	[[nodiscard]] bool HasSameRayTransforms(const RayScenePlan& a, const RayScenePlan& b) noexcept;
	[[nodiscard]] bool HasSameRayInstances(const RayScenePlan& a, const RayScenePlan& b) noexcept;
}
