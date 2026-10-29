#include "Graphics/RayScenePlan.h"
#include "GGLabRuntime/Graphics/VertexData.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <tuple>

namespace gglab
{
	namespace
	{
		bool EqualMaterial(const MaterialGPU& a, const MaterialGPU& b) noexcept
		{
			return std::memcmp(&a, &b, sizeof(MaterialGPU)) == 0;
		}

		auto SourceIdentity(const RaySceneSource& source) noexcept
		{
			return std::tuple(source.m_EntityIdentity, source.m_Model.Value(),
				source.m_ModelGeneration, source.m_ModelMeshIndex);
		}
	}

	RaySceneStatus PlanRayScene(std::vector<RaySceneSource> sources,
		const RaySceneCapacity& capacity, const RHIASBuildLimits& limits,
		RayScenePlan& result, std::string& reason) noexcept
	{
		result = {};
		reason.clear();
		auto fail = [&reason](RaySceneStatus status, const char* message) noexcept
			{
				reason = message;
				return status;
			};
		if (sources.empty())
		{
			return fail(RaySceneStatus::NotReady, "The World has no Ready ray instances.");
		}
		if (sources.size() > capacity.m_MaxInstances || sources.size() > 0x1000000ull)
		{
			return fail(RaySceneStatus::CapacityExceeded, "Ray instance table capacity exceeded.");
		}
		std::sort(sources.begin(), sources.end(), [](const auto& a, const auto& b)
			{
				return SourceIdentity(a) < SourceIdentity(b);
			});
		RayScenePlan plan;
		std::vector<RenderMaterialKey> materialKeys;
		for (size_t index = 0; index < sources.size(); ++index)
		{
			const auto& source = sources[index];
			if (index != 0 && SourceIdentity(sources[index - 1]) == SourceIdentity(source))
			{
				return fail(RaySceneStatus::InvalidSource, "Duplicate ray instance identity.");
			}
			if (source.m_AlphaMode == AlphaMode::Mask)
			{
				return fail(RaySceneStatus::UnsupportedGeometry, "MASK ray geometry is unsupported.");
			}
			if ((source.m_AlphaMode != AlphaMode::Opaque && source.m_AlphaMode != AlphaMode::Blend) ||
				!source.m_Model.IsValid() || !source.m_Geometry.m_Mesh.IsValid() ||
				!source.m_MaterialKey.IsValid())
			{
				return fail(RaySceneStatus::InvalidSource, "Invalid ray source identity or alpha mode.");
			}
			RHIASInstanceDesc instance{ .m_BottomLevel = { 0, 1 },
				.m_Transform = source.m_Transform };
			if (!ValidateRHIASInstances(std::span(&instance, 1), limits).IsValid())
			{
				return fail(RaySceneStatus::InvalidSource, "Invalid ray instance transform.");
			}
			const auto material = std::find(materialKeys.begin(), materialKeys.end(), source.m_MaterialKey);
			const auto materialIndex = static_cast<uint32_t>(material - materialKeys.begin());
			if (material == materialKeys.end())
			{
				if (materialKeys.size() >= capacity.m_MaxMaterials)
				{
					return fail(RaySceneStatus::CapacityExceeded, "Ray material table capacity exceeded.");
				}
				materialKeys.push_back(source.m_MaterialKey);
				plan.m_Materials.push_back(source.m_Material);
			}
			else if (!EqualMaterial(plan.m_Materials[materialIndex], source.m_Material))
			{
				return fail(RaySceneStatus::InvalidSource, "Conflicting ray material identities.");
			}
			RaySceneInstanceGPU entry{ .m_MaterialIndex = materialIndex };
			if (source.m_AlphaMode == AlphaMode::Blend)
			{
				++plan.m_BlendCount;
				plan.m_Instances.push_back(entry);
				continue;
			}
			const auto geometry = std::find_if(plan.m_Geometries.begin(), plan.m_Geometries.end(),
				[&source](const auto& value)
				{
					return value.m_Key == source.m_Geometry;
				});
			entry.m_GeometryIndex = static_cast<uint32_t>(geometry - plan.m_Geometries.begin());
			entry.m_Flags = 1;
			if (geometry == plan.m_Geometries.end())
			{
				const bool mixedRoles = source.m_Vertices.m_Buffer == source.m_Indices.m_Buffer ||
					std::any_of(plan.m_Geometries.begin(), plan.m_Geometries.end(), [&source](const auto& existing)
						{
							return existing.m_SourceVertices.m_Buffer == source.m_Indices.m_Buffer ||
								existing.m_SourceIndices.m_Buffer == source.m_Vertices.m_Buffer;
						});
				if (mixedRoles)
				{
					return fail(RaySceneStatus::UnsupportedGeometry,
						"Ray source allocations cannot mix vertex and index raster states.");
				}
				if (plan.m_Geometries.size() >= capacity.m_MaxGeometries)
				{
					return fail(RaySceneStatus::CapacityExceeded, "Ray geometry capacity exceeded.");
				}
				const uint64_t vertexBytes = uint64_t(source.m_VertexCount) * sizeof(Vertex);
				const uint64_t indexBytes = uint64_t(source.m_IndexCount) * sizeof(uint32_t);
				if (vertexBytes > capacity.m_MaxVertexBytes ||
					plan.m_VertexBytes > capacity.m_MaxVertexBytes - vertexBytes ||
					indexBytes > capacity.m_MaxIndexBytes ||
					plan.m_IndexBytes > capacity.m_MaxIndexBytes - indexBytes ||
					plan.m_VertexBytes + vertexBytes > UINT32_MAX ||
					plan.m_IndexBytes + indexBytes > UINT32_MAX)
				{
					return fail(RaySceneStatus::CapacityExceeded, "Ray geometry byte pool capacity exceeded.");
				}
				// Bindings describe a logical subrange; allocation extents come from the device.
				const auto& vertices = source.m_VertexBufferDesc;
				const auto& indices = source.m_IndexBufferDesc;
				const RHIASIndexedTriangles triangles{
					.m_Vertices = { source.m_Vertices.m_Buffer, source.m_Vertices.m_Offset, vertexBytes },
					.m_VertexCount = source.m_VertexCount,
					.m_VertexStrideInBytes = source.m_Vertices.m_Stride,
					.m_Indices = { source.m_Indices.m_Buffer, source.m_Indices.m_Offset, indexBytes },
					.m_IndexCount = source.m_IndexCount, .m_IndexFormat = source.m_Indices.m_Format };
				if (source.m_Vertices.m_Stride != sizeof(Vertex) ||
					!Test(vertices.m_Usage, RHIBufferUsage::CopySource) ||
					!Test(indices.m_Usage, RHIBufferUsage::CopySource) ||
					source.m_Vertices.m_Offset > vertices.m_SizeInBytes ||
					source.m_Vertices.m_SizeInBytes > vertices.m_SizeInBytes - source.m_Vertices.m_Offset ||
					source.m_Indices.m_Offset > indices.m_SizeInBytes ||
					source.m_Indices.m_SizeInBytes > indices.m_SizeInBytes - source.m_Indices.m_Offset ||
					vertexBytes > source.m_Vertices.m_SizeInBytes || indexBytes > source.m_Indices.m_SizeInBytes ||
					!ValidateRHIASIndexedTriangles(triangles, vertices, indices, limits).IsValid())
				{
					return fail(RaySceneStatus::InvalidSource, "Invalid ray mesh layout or buffer range.");
				}
				plan.m_Geometries.push_back({ source.m_Geometry, source.m_Vertices, source.m_Indices,
					vertices, indices,
					{ static_cast<uint32_t>(plan.m_VertexBytes), static_cast<uint32_t>(plan.m_IndexBytes),
						source.m_IndexCount / 3, source.m_VertexCount } });
				plan.m_VertexBytes += vertexBytes;
				plan.m_IndexBytes += indexBytes;
			}
			else if (geometry->m_GPU.m_VertexCount != source.m_VertexCount ||
				geometry->m_GPU.m_TriangleCount * 3 != source.m_IndexCount ||
				geometry->m_SourceVertices != source.m_Vertices || geometry->m_SourceIndices != source.m_Indices)
			{
				return fail(RaySceneStatus::InvalidSource, "Conflicting ray mesh content identities.");
			}
			plan.m_Instances.push_back(entry);
			++plan.m_OpaqueCount;
		}
		if (plan.m_OpaqueCount > limits.m_MaxInstanceCount)
		{
			return fail(RaySceneStatus::CapacityExceeded, "Native TLAS instance capacity exceeded.");
		}
		if (plan.m_OpaqueCount == 0)
		{
			return fail(RaySceneStatus::NotReady, "The World has no opaque ray instances.");
		}
		// Pool layout depends on geometry identities, not entity traversal or visibility.
		const auto unsortedGeometry = plan.m_Geometries;
		std::sort(plan.m_Geometries.begin(), plan.m_Geometries.end(), [](const auto& a, const auto& b)
			{
				return std::tuple(a.m_Key.m_Mesh.Value(), a.m_Key.m_ContentGeneration) <
					std::tuple(b.m_Key.m_Mesh.Value(), b.m_Key.m_ContentGeneration);
			});
		uint32_t vertexBase = 0;
		uint32_t indexBase = 0;
		for (auto& geometry : plan.m_Geometries)
		{
			geometry.m_GPU.m_VertexBaseInBytes = vertexBase;
			geometry.m_GPU.m_IndexBaseInBytes = indexBase;
			vertexBase += geometry.m_GPU.m_VertexCount * sizeof(Vertex);
			indexBase += geometry.m_GPU.m_TriangleCount * 3 * sizeof(uint32_t);
		}
		for (auto& instance : plan.m_Instances)
		{
			if (instance.m_Flags != 0)
			{
				const auto key = unsortedGeometry[instance.m_GeometryIndex].m_Key;
				const auto found = std::find_if(plan.m_Geometries.begin(), plan.m_Geometries.end(),
					[key](const auto& geometry)
					{
						return geometry.m_Key == key;
					});
				instance.m_GeometryIndex = static_cast<uint32_t>(found - plan.m_Geometries.begin());
			}
		}
		plan.m_Sources = std::move(sources);
		result = std::move(plan);
		return RaySceneStatus::Prepared;
	}

	bool HasSameRayGeometry(const RayScenePlan& a, const RayScenePlan& b) noexcept
	{
		if (a.m_Geometries.size() != b.m_Geometries.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.m_Geometries.size(); ++index)
		{
			const auto& x = a.m_Geometries[index];
			const auto& y = b.m_Geometries[index];
			if (x.m_Key != y.m_Key || x.m_GPU.m_VertexCount != y.m_GPU.m_VertexCount ||
				x.m_GPU.m_TriangleCount != y.m_GPU.m_TriangleCount)
			{
				return false;
			}
		}
		return true;
	}

	bool HasSameRayTransforms(const RayScenePlan& a, const RayScenePlan& b) noexcept
	{
		if (a.m_Sources.size() != b.m_Sources.size() || !HasSameRayGeometry(a, b))
		{
			return false;
		}
		for (size_t index = 0; index < a.m_Sources.size(); ++index)
		{
			const auto& x = a.m_Sources[index];
			const auto& y = b.m_Sources[index];
			if (SourceIdentity(x) != SourceIdentity(y) || x.m_Geometry != y.m_Geometry ||
				x.m_Transform != y.m_Transform || x.m_AlphaMode != y.m_AlphaMode)
			{
				return false;
			}
		}
		return true;
	}

	bool HasSameRayInstances(const RayScenePlan& a, const RayScenePlan& b) noexcept
	{
		if (!HasSameRayTransforms(a, b))
		{
			return false;
		}
		for (size_t index = 0; index < a.m_Sources.size(); ++index)
		{
			const auto& x = a.m_Sources[index];
			const auto& y = b.m_Sources[index];
			if (x.m_MaterialKey != y.m_MaterialKey || !EqualMaterial(x.m_Material, y.m_Material))
			{
				return false;
			}
		}
		return true;
	}
}
