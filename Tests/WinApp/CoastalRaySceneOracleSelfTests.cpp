#include "CoastalRaySceneOracleSelfTests.h"
#include "WinAppTestRunner.h"
#include "Application/Content/RuntimeAssetBundle.h"
#include "Application/Lab/Validation/CoastalRaySceneOracle.h"
#include "GGLabRuntime/Core/Math/Transform.h"
#include "GGLabRuntime/Graphics/Asset/AssetPaths.h"
#include "GGLabRuntime/Graphics/Asset/ModelImporter.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace gglab
{
	namespace
	{
		struct Point
		{
			double x, y, z;
		};
		Point Subtract(Point a, Point b) noexcept
		{
			return { a.x - b.x, a.y - b.y, a.z - b.z };
		}
		Point Cross(Point a, Point b) noexcept
		{
			return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
		}
		double Dot(Point a, Point b) noexcept
		{
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}
		Point Position(const Vector3& p) noexcept
		{
			return { p.m_X, p.m_Y, p.m_Z };
		}

		// Independent CPU triangle intersection; never calls RayScenePlan or an RHI backend.
		CoastalProbeHit Trace(const ImportedModel& model, uint32_t rayIndex) noexcept
		{
			const Vector3 worldOrigin(float(rayIndex % CoastalProbeWidth) + 0.5f - 16.0f,
				20.0f, float(rayIndex / CoastalProbeWidth) + 0.5f - 16.0f);
			CoastalProbeHit result;
			double closest = 100.0;
			for (uint32_t instanceIndex = 0; instanceIndex < model.m_MeshInstances.size(); ++instanceIndex)
			{
				const auto& instance = model.m_MeshInstances[instanceIndex];
				if (model.m_Materials[instance.m_MaterialIndex].m_Properties.m_AlphaMode != AlphaMode::Opaque)
				{
					continue;
				}
				Matrix inverse;
				if (!math::TryInverse(instance.m_LocalTransform, inverse))
				{
					continue;
				}
				const auto origin = Position(math::TransformPoint(worldOrigin, inverse));
				const auto direction = Position(math::TransformDirection(Vector3(0, -1, 0), inverse));
				const auto& mesh = model.m_Meshes[instance.m_MeshIndex];
				const std::array origins{ origin.x, origin.y, origin.z };
				const std::array directions{ direction.x, direction.y, direction.z };
				const std::array centers{ mesh.m_Aabb.m_Center.m_X, mesh.m_Aabb.m_Center.m_Y, mesh.m_Aabb.m_Center.m_Z };
				const std::array extents{ mesh.m_Aabb.m_Extents.m_X, mesh.m_Aabb.m_Extents.m_Y, mesh.m_Aabb.m_Extents.m_Z };
				double nearT = 0.001;
				double farT = closest;
				for (size_t axis = 0; axis < 3; ++axis)
				{
					const double low = double(centers[axis]) - extents[axis] - 1e-3;
					const double high = double(centers[axis]) + extents[axis] + 1e-3;
					if (std::abs(directions[axis]) < 1e-14)
					{
						if (origins[axis] < low || origins[axis] > high)
						{
							farT = -1;
						}
					}
					else
					{
						const double a = (low - origins[axis]) / directions[axis];
						const double b = (high - origins[axis]) / directions[axis];
						nearT = std::max(nearT, std::min(a, b));
						farT = std::min(farT, std::max(a, b));
					}
				}
				if (farT < nearT)
				{
					continue;
				}
				for (uint32_t triangle = 0; triangle < mesh.m_Indices.size() / 3; ++triangle)
				{
					const auto a = Position(mesh.m_Vertices[mesh.m_Indices[triangle * 3]].m_Position);
					const auto b = Position(mesh.m_Vertices[mesh.m_Indices[triangle * 3 + 1]].m_Position);
					const auto c = Position(mesh.m_Vertices[mesh.m_Indices[triangle * 3 + 2]].m_Position);
					const auto edge1 = Subtract(b, a);
					const auto edge2 = Subtract(c, a);
					const auto p = Cross(direction, edge2);
					const double determinant = Dot(edge1, p);
					if (std::abs(determinant) < 1e-14)
					{
						continue;
					}
					const auto offset = Subtract(origin, a);
					const double u = Dot(offset, p) / determinant;
					const auto q = Cross(offset, edge1);
					const double v = Dot(direction, q) / determinant;
					const double distance = Dot(edge2, q) / determinant;
					if (u >= 0 && v >= 0 && u + v <= 1 && distance >= 0.001 && distance < closest)
					{
						closest = distance;
						result = { 1, instanceIndex, triangle, 0, { float(u), float(v) }, float(distance), 0 };
					}
				}
			}
			return result;
		}
	}

	void RunCoastalRaySceneOracleSelfTests(SelfTestContext& context) noexcept
	{
		std::string reason;
		const auto bundle = LoadRuntimeAssetBundle(GetApplicationSelfTestAssetRoot() /
			"Models/GGLabCoastalRetreat/bundle.json", reason);
		context.Check(bundle && bundle->m_Id == CoastalOracleBundleId && bundle->m_Revision == CoastalOracleBundleRevision,
			"Coastal oracle is bound to the reviewed installed asset bundle");
		if (!bundle)
		{
			return;
		}
		const auto imported = ModelImporter::Import(ResolveAssetPath(GetApplicationSelfTestAssetRoot(), bundle->m_EntryPoint), {});
		context.Check(imported.Succeeded(), "Coastal oracle CPU reference import succeeds");
		if (!imported.Succeeded())
		{
			return;
		}
		const auto& model = imported.m_Model;
		std::map<uint32_t, uint32_t> geometryIndices;
		for (const auto& instance : model.m_MeshInstances)
		{
			if (model.m_Materials[instance.m_MaterialIndex].m_Properties.m_AlphaMode == AlphaMode::Opaque)
			{
				geometryIndices.emplace(instance.m_MeshIndex, 0);
			}
		}
		std::vector<RaySceneGeometryGPU> geometries;
		uint32_t vertexBase = 0;
		uint32_t indexBase = 0;
		for (auto& [meshIndex, tableIndex] : geometryIndices)
		{
			const auto& mesh = model.m_Meshes[meshIndex];
			tableIndex = static_cast<uint32_t>(geometries.size());
			geometries.push_back({ vertexBase, indexBase, uint32_t(mesh.m_Indices.size() / 3), uint32_t(mesh.m_Vertices.size()) });
			vertexBase += uint32_t(mesh.m_Vertices.size() * sizeof(Vertex));
			indexBase += uint32_t(mesh.m_Indices.size() * sizeof(uint32_t));
		}
		std::map<uint32_t, uint32_t> materials;
		std::vector<RaySceneInstanceGPU> instances;
		for (const auto& instance : model.m_MeshInstances)
		{
			const auto [material, inserted] = materials.emplace(instance.m_MaterialIndex, uint32_t(materials.size()));
			GGLAB_UNUSED(inserted);
			const bool opaque = model.m_Materials[instance.m_MaterialIndex].m_Properties.m_AlphaMode == AlphaMode::Opaque;
			instances.push_back({ opaque ? geometryIndices.at(instance.m_MeshIndex) : UINT32_MAX,
				material->second, opaque ? 1u : 0u, 0 });
		}
		context.Check(ValidateCoastalRaySceneTables(std::as_bytes(std::span(geometries)),
			std::as_bytes(std::span(instances)), reason), "Independent CPU import matches the complete frozen integer table digests");
		std::array<CoastalProbeHit, CoastalProbeRayCount> hits{};
		for (const auto& reference : CoastalProbeReferences)
		{
			hits[reference.m_RayIndex] = Trace(model, reference.m_RayIndex);
		}
		const bool matches = ValidateCoastalRaySceneHits(hits, reason);
		context.Check(matches, "Frozen GPU reference rays agree with independent CPU intersections: " + reason);
		const auto baseline = hits;
		const auto index = CoastalProbeReferences.front().m_RayIndex;
		hits[index] = {};
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "A missing sampled object fails even while other rays hit");
		hits = baseline;
		++hits[index].m_Instance;
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "An in-range wrong instance ID fails the oracle");
		hits = baseline;
		--hits[index].m_Primitive;
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "An in-range wrong primitive ID fails the oracle");
		hits = baseline;
		hits[index].m_Distance += 0.1f;
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "A finite but wrong hit distance fails the oracle");
		hits = baseline;
		hits[index].m_Barycentrics[0] += 0.01f;
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "Valid-range but wrong barycentrics fail the oracle");
		hits = baseline;
		hits.back() = baseline[index];
		context.Check(!ValidateCoastalRaySceneHits(hits, reason), "A zero-mask or upward-ray false hit fails the oracle");
		hits = baseline;
		hits[index].m_Distance += CoastalOracleDistanceTolerance * 0.25f;
		context.Check(ValidateCoastalRaySceneHits(hits, reason), "Sub-tolerance floating-point variation preserves the reference hit");
		const auto savedInstance = instances.back();
		instances.back().m_GeometryIndex = (instances.back().m_GeometryIndex + 1) % CoastalOracleGeometryCount;
		context.Check(!ValidateCoastalRaySceneTables(std::as_bytes(std::span(geometries)),
			std::as_bytes(std::span(instances)), reason), "An in-range change to an unsampled instance fails the frozen inventory");
		instances.back() = savedInstance;
		instances.pop_back();
		context.Check(!ValidateCoastalRaySceneTables(std::as_bytes(std::span(geometries)),
			std::as_bytes(std::span(instances)), reason), "Removing an unsampled instance fails the table inventory");
	}
}
