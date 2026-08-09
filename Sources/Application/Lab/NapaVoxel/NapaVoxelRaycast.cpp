#include "Core/Precompiled.h"
#include "Application/Lab/NapaVoxel/NapaVoxelRaycast.h"

#include "Application/Lab/NapaVoxel/NapaVoxelMeshAdapter.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace gglab
{
	namespace
	{
		struct RaycastCandidate
		{
			NapaVoxelRaycastHit m_Hit{};
		};

		[[nodiscard]] bool IntersectTriangle(napa::voxel::Double3 origin,
			napa::voxel::Double3 direction, napa::voxel::Double3 a,
			napa::voxel::Double3 b, napa::voxel::Double3 c, double& distance) noexcept
		{
			using namespace napa::voxel;
			const Double3 edgeAB = b - a;
			const Double3 edgeAC = c - a;
			const Double3 p = Cross(direction, edgeAC);
			const double determinant = Dot(edgeAB, p);
			if (!std::isfinite(determinant) || determinant == 0.0)
			{
				return false;
			}

			const double inverseDeterminant = 1.0 / determinant;
			const Double3 fromA = origin - a;
			const double barycentricU = Dot(fromA, p) * inverseDeterminant;
			if (!std::isfinite(barycentricU) || barycentricU < 0.0 || barycentricU > 1.0)
			{
				return false;
			}
			const Double3 q = Cross(fromA, edgeAB);
			const double barycentricV = Dot(direction, q) * inverseDeterminant;
			if (!std::isfinite(barycentricV) || barycentricV < 0.0 ||
				barycentricU + barycentricV > 1.0)
			{
				return false;
			}

			const double candidateDistance = Dot(edgeAC, q) * inverseDeterminant;
			if (!std::isfinite(candidateDistance) || candidateDistance < 0.0)
			{
				return false;
			}
			distance = candidateDistance;
			return true;
		}

		[[nodiscard]] std::optional<std::int64_t> MakeDistanceKey(
			double distance, double tieQuantum) noexcept
		{
			constexpr double LargestRepresentableInt64BelowMaximum =
				9'223'372'036'854'774'784.0;
			const double scaled = distance / tieQuantum;
			if (!std::isfinite(scaled) || scaled < 0.0 ||
				scaled > LargestRepresentableInt64BelowMaximum)
			{
				return std::nullopt;
			}
			return static_cast<std::int64_t>(std::floor(scaled + 0.5));
		}

		[[nodiscard]] bool IsCandidateLess(
			const RaycastCandidate& lhs, const RaycastCandidate& rhs) noexcept
		{
			if (lhs.m_Hit.m_DistanceKey != rhs.m_Hit.m_DistanceKey)
			{
				return lhs.m_Hit.m_DistanceKey < rhs.m_Hit.m_DistanceKey;
			}
			if (lhs.m_Hit.m_Chunk != rhs.m_Hit.m_Chunk)
			{
				return napa::voxel::ChunkCoordZYXLess{}(
					lhs.m_Hit.m_Chunk, rhs.m_Hit.m_Chunk);
			}
			const std::uint8_t lhsMaterial = static_cast<std::uint8_t>(lhs.m_Hit.m_Material);
			const std::uint8_t rhsMaterial = static_cast<std::uint8_t>(rhs.m_Hit.m_Material);
			if (lhsMaterial != rhsMaterial)
			{
				return lhsMaterial < rhsMaterial;
			}
			if (lhs.m_Hit.m_SectionOrdinal != rhs.m_Hit.m_SectionOrdinal)
			{
				return lhs.m_Hit.m_SectionOrdinal < rhs.m_Hit.m_SectionOrdinal;
			}
			return lhs.m_Hit.m_TriangleOrdinal < rhs.m_Hit.m_TriangleOrdinal;
		}
	}

	bool IsValidNapaVoxelRay(const NapaVoxelRay& ray) noexcept
	{
		napa::voxel::Double3 normalized{};
		return napa::voxel::IsFinite(ray.m_Origin) &&
			napa::voxel::TryNormalize(ray.m_Direction, normalized);
	}

	NapaVoxelRaycastResult RaycastNapaVoxelMeshRecords(
		const napa::voxel::VoxelWorldConfig& config,
		std::span<const napa::voxel::ChunkMeshRecord> records,
		const NapaVoxelRay& ray, NapaVoxelRaycastHit& hit) noexcept
	{
		if (napa::voxel::ValidateConfig(config).Failed())
		{
			return { .m_Error = NapaVoxelRaycastError::InvalidConfig };
		}
		napa::voxel::Double3 direction{};
		if (!napa::voxel::IsFinite(ray.m_Origin) ||
			!napa::voxel::TryNormalize(ray.m_Direction, direction))
		{
			return { .m_Error = NapaVoxelRaycastError::InvalidRay };
		}

		const double tieQuantum = std::max(1.0e-9,
			static_cast<double>(config.m_VoxelSize) * 1.0e-9);
		std::optional<RaycastCandidate> best;
		for (const napa::voxel::ChunkMeshRecord& record : records)
		{
			NapaVoxelWorldPosition chunkOrigin{};
			if (ComputeNapaVoxelChunkOrigin(config, record.m_Chunk, chunkOrigin).Failed())
			{
				continue;
			}
			const napa::voxel::Double3 localOrigin{
				ray.m_Origin.m_X - chunkOrigin.m_X,
				ray.m_Origin.m_Y - chunkOrigin.m_Y,
				ray.m_Origin.m_Z - chunkOrigin.m_Z,
			};
			for (std::size_t sectionIndex = 0;
				sectionIndex < record.m_Mesh.m_Sections.size(); ++sectionIndex)
			{
				const napa::voxel::MeshSection& section = record.m_Mesh.m_Sections[sectionIndex];
				for (std::size_t index = 0; index + 2 < section.m_Indices.size(); index += 3)
				{
					const std::uint32_t indexA = section.m_Indices[index];
					const std::uint32_t indexB = section.m_Indices[index + 1];
					const std::uint32_t indexC = section.m_Indices[index + 2];
					if (indexA >= record.m_Mesh.m_Vertices.size() ||
						indexB >= record.m_Mesh.m_Vertices.size() ||
						indexC >= record.m_Mesh.m_Vertices.size())
					{
						continue;
					}
					const auto toDouble = [](napa::voxel::Float3 value) noexcept
						{
							return napa::voxel::Double3{
								static_cast<double>(value.m_X),
								static_cast<double>(value.m_Y),
								static_cast<double>(value.m_Z),
							};
						};
					double distance = 0.0;
					if (!IntersectTriangle(localOrigin, direction,
						toDouble(record.m_Mesh.m_Vertices[indexA].m_Position),
						toDouble(record.m_Mesh.m_Vertices[indexB].m_Position),
						toDouble(record.m_Mesh.m_Vertices[indexC].m_Position), distance))
					{
						continue;
					}
					const std::optional<std::int64_t> distanceKey =
						MakeDistanceKey(distance, tieQuantum);
					if (!distanceKey)
					{
						continue;
					}

					RaycastCandidate candidate{
						.m_Hit = {
							.m_WorldPosition = {
								ray.m_Origin.m_X + direction.m_X * distance,
								ray.m_Origin.m_Y + direction.m_Y * distance,
								ray.m_Origin.m_Z + direction.m_Z * distance,
							},
							.m_Distance = distance,
							.m_DistanceKey = *distanceKey,
							.m_Chunk = record.m_Chunk,
							.m_Material = section.m_Material,
							.m_SectionOrdinal = sectionIndex,
							.m_TriangleOrdinal = index / 3,
						},
					};
					if (!best || IsCandidateLess(candidate, *best))
					{
						best = candidate;
					}
				}
			}
		}

		if (!best)
		{
			return {};
		}
		hit = best->m_Hit;
		return { .m_Hit = true };
	}

	NapaVoxelRaycastResult RaycastNapaVoxelVisibleMesh(
		const napa::voxel::VisibleMeshSet& visible,
		const NapaVoxelRay& ray, NapaVoxelRaycastHit& hit) noexcept
	{
		if (!visible.HasPublishedMeshes())
		{
			return { .m_Error = NapaVoxelRaycastError::UninitializedVisibleMesh };
		}
		return RaycastNapaVoxelMeshRecords(
			visible.GetConfig(), visible.GetChunks(), ray, hit);
	}
}
