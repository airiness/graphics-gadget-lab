#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGResource.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gglab
{
	class AssetManager;
	class RHIDevice;
	class RenderGraph;
	class RenderSamplerAccess;
	class World;

	struct RaySceneCapacity
	{
		uint32_t m_MaxGeometries = 4096;
		uint32_t m_MaxInstances = 4096;
		uint32_t m_MaxMaterials = 1024;
		uint64_t m_MaxVertexBytes = 128ull * 1024 * 1024;
		uint64_t m_MaxIndexBytes = 64ull * 1024 * 1024;
	};

	struct RaySceneGeometryGPU
	{
		uint32_t m_VertexBaseInBytes = 0;
		uint32_t m_IndexBaseInBytes = 0;
		uint32_t m_TriangleCount = 0;
		uint32_t m_VertexCount = 0;
	};

	struct RaySceneInstanceGPU
	{
		uint32_t m_GeometryIndex = UINT32_MAX;
		uint32_t m_MaterialIndex = 0;
		// Bit zero is the opaque ray mask. Excluded BLEND entries have no TLAS record.
		uint32_t m_Flags = 0;
		uint32_t m_Reserved = 0;
	};
	static_assert(sizeof(RaySceneGeometryGPU) == 16);
	static_assert(sizeof(RaySceneInstanceGPU) == 16);

	enum class RaySceneStatus : uint8_t
	{
		NotReady,
		Prepared,
		Unavailable,
		UnsupportedGeometry,
		CapacityExceeded,
		InvalidSource,
		AllocationFailed,
	};

	struct RaySceneDiagnostics
	{
		RaySceneStatus m_Status = RaySceneStatus::NotReady;
		std::string m_Reason;
		uint64_t m_Revision = 0;
		uint64_t m_GeometryRevision = 0;
		uint64_t m_BottomLevelBuildCount = 0;
		uint64_t m_TopLevelBuildCount = 0;
		uint64_t m_GeometryCopyBytes = 0;
		uint32_t m_GeometryCount = 0;
		uint32_t m_InstanceCount = 0;
		uint32_t m_OpaqueInstanceCount = 0;
		uint32_t m_ExcludedBlendCount = 0;
		uint32_t m_MaterialCount = 0;
		uint64_t m_VertexBytes = 0;
		uint64_t m_IndexBytes = 0;
	};

	// Graph-local imports. Declare all traversed BLAS reads as well as the TLAS read.
	// Retain m_Lifetime with the query encoder's RetainForSubmission before recording use.
	// Pools and tables finish in StructuredRead; acceleration structures in QueryRead.
	struct RaySceneGraphResources
	{
		uint32_t m_GeometryCount = 0;
		uint32_t m_InstanceCount = 0;
		uint32_t m_MaterialCount = 0;
		RGAccelerationStructureId m_TopLevel;
		std::vector<RGAccelerationStructureId> m_BottomLevels;
		RGBufferId m_Vertices;
		RGBufferId m_Indices;
		RGBufferId m_Geometries;
		RGBufferId m_Instances;
		RGBufferId m_Materials;
		std::shared_ptr<void> m_Lifetime;
	};

	// Owner-thread service, with one instance per World/session. Preparation observes
	// the entire World, never a view's culled draws. Keep source asset interests through
	// recording; copied geometry survives raster residency. Only ray consumers invoke it.
	// Geometry layouts are immutable; transforms/materials replace tables and the TLAS
	// while sharing unchanged pools/BLAS. A changed geometry layout replaces that cache.
	class RayScene final
	{
	public:
		explicit RayScene(RaySceneCapacity capacity = {}) noexcept;
		~RayScene();
		GGLAB_DELETE_COPYABLE_MOVABLE(RayScene);
		[[nodiscard]] RaySceneStatus Prepare(const World& world, AssetManager& assets,
			RHIDevice& device, const RenderSamplerAccess& samplers) noexcept;
		[[nodiscard]] RaySceneGraphResources AddBuildPasses(RenderGraph& graph) noexcept;
		[[nodiscard]] const RaySceneDiagnostics& GetDiagnostics() const noexcept;

	private:
		struct State;
		std::unique_ptr<State> m_State;
	};
}
