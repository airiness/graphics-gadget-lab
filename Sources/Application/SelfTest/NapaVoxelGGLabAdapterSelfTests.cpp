#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "Application/Lab/NapaVoxel/NapaVoxelMeshAdapter.h"

#include "NapaVoxelCore/Field/Primitive.h"
#include "NapaVoxelCore/Meshing/ReferenceMesher.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeSingleChunkConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = {},
					.m_MaxExclusive = { 16, 16, 16 },
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeNegativeChunkConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -16, 0, 0 },
					.m_MaxExclusive = { 0, 16, 16 },
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeEightChunkConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 1.0f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 8, 8, 8 },
				},
			};
		}

		[[nodiscard]] napa::voxel::PrimitiveDesc MakeSphere(std::uint64_t stableId,
			napa::voxel::VoxelMaterial material, napa::voxel::Double3 center,
			double radius) noexcept
		{
			return {
				.m_StableId = { stableId },
				.m_Material = material,
				.m_Shape = napa::voxel::PrimitiveShape::Sphere,
				.m_Parameters = {
					.m_Sphere = {
						.m_Center = center,
						.m_Radius = radius,
					},
				},
			};
		}

		[[nodiscard]] bool BuildWorldMeshes(const napa::voxel::VoxelWorldConfig& config,
			std::span<const napa::voxel::PrimitiveDesc> primitives,
			napa::voxel::ReferenceWorldMeshingResult& meshing)
		{
			std::unique_ptr<napa::voxel::VoxelWorld> world;
			napa::voxel::PrimitiveWorldGenerationResult generation{};
			return napa::voxel::GeneratePrimitiveVoxelWorld(
				config, primitives, world, generation).Succeeded() &&
				world && napa::voxel::ReferenceMesher(*world).MeshWorld(meshing).Succeeded();
		}

		[[nodiscard]] bool MatchesVertices(const napa::voxel::MeshData& source,
			const NapaVoxelCpuChunkMesh& converted) noexcept
		{
			if (source.m_Vertices.size() != converted.m_Vertices.size())
			{
				return false;
			}
			for (std::size_t index = 0; index < source.m_Vertices.size(); ++index)
			{
				const napa::voxel::MeshVertex& sourceVertex = source.m_Vertices[index];
				const NapaVoxelRenderVertex& convertedVertex = converted.m_Vertices[index];
				if (convertedVertex.m_Position.m_X != sourceVertex.m_Position.m_X ||
					convertedVertex.m_Position.m_Y != sourceVertex.m_Position.m_Y ||
					convertedVertex.m_Position.m_Z != sourceVertex.m_Position.m_Z ||
					convertedVertex.m_Normal.m_X != sourceVertex.m_Normal.m_X ||
					convertedVertex.m_Normal.m_Y != sourceVertex.m_Normal.m_Y ||
					convertedVertex.m_Normal.m_Z != sourceVertex.m_Normal.m_Z)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool MatchesSectionRanges(const napa::voxel::MeshData& source,
			const NapaVoxelCpuChunkMesh& converted) noexcept
		{
			if (source.m_Sections.size() != converted.m_SectionDrawRanges.size())
			{
				return false;
			}

			std::size_t firstIndex = 0;
			for (std::size_t sectionIndex = 0; sectionIndex < source.m_Sections.size(); ++sectionIndex)
			{
				const napa::voxel::MeshSection& sourceSection = source.m_Sections[sectionIndex];
				const NapaVoxelSectionDrawRange& range =
					converted.m_SectionDrawRanges[sectionIndex];
				if (range.m_Material != sourceSection.m_Material ||
					range.m_FirstIndex != firstIndex ||
					range.m_IndexCount != sourceSection.m_Indices.size())
				{
					return false;
				}
				if (!std::equal(sourceSection.m_Indices.begin(), sourceSection.m_Indices.end(),
					converted.m_Indices.begin() + firstIndex))
				{
					return false;
				}
				firstIndex += sourceSection.m_Indices.size();
			}
			return firstIndex == converted.m_Indices.size();
		}

		[[nodiscard]] bool MatchesChunkMesh(const NapaVoxelCpuChunkMesh& lhs,
			const NapaVoxelCpuChunkMesh& rhs) noexcept
		{
			if (lhs.m_Chunk != rhs.m_Chunk ||
				lhs.m_SourceWorldVoxelRevision != rhs.m_SourceWorldVoxelRevision ||
				lhs.m_ChunkOrigin != rhs.m_ChunkOrigin ||
				lhs.m_CoreValidation != rhs.m_CoreValidation ||
				lhs.m_Indices != rhs.m_Indices ||
				lhs.m_Vertices.size() != rhs.m_Vertices.size() ||
				lhs.m_SectionDrawRanges.size() != rhs.m_SectionDrawRanges.size() ||
				lhs.m_LocalBounds.m_Min.m_X != rhs.m_LocalBounds.m_Min.m_X ||
				lhs.m_LocalBounds.m_Min.m_Y != rhs.m_LocalBounds.m_Min.m_Y ||
				lhs.m_LocalBounds.m_Min.m_Z != rhs.m_LocalBounds.m_Min.m_Z ||
				lhs.m_LocalBounds.m_Max.m_X != rhs.m_LocalBounds.m_Max.m_X ||
				lhs.m_LocalBounds.m_Max.m_Y != rhs.m_LocalBounds.m_Max.m_Y ||
				lhs.m_LocalBounds.m_Max.m_Z != rhs.m_LocalBounds.m_Max.m_Z)
			{
				return false;
			}
			for (std::size_t index = 0; index < lhs.m_Vertices.size(); ++index)
			{
				const NapaVoxelRenderVertex& a = lhs.m_Vertices[index];
				const NapaVoxelRenderVertex& b = rhs.m_Vertices[index];
				if (a.m_Position.m_X != b.m_Position.m_X ||
					a.m_Position.m_Y != b.m_Position.m_Y ||
					a.m_Position.m_Z != b.m_Position.m_Z ||
					a.m_Normal.m_X != b.m_Normal.m_X ||
					a.m_Normal.m_Y != b.m_Normal.m_Y ||
					a.m_Normal.m_Z != b.m_Normal.m_Z)
				{
					return false;
				}
			}
			for (std::size_t index = 0; index < lhs.m_SectionDrawRanges.size(); ++index)
			{
				const NapaVoxelSectionDrawRange& a = lhs.m_SectionDrawRanges[index];
				const NapaVoxelSectionDrawRange& b = rhs.m_SectionDrawRanges[index];
				if (a.m_Material != b.m_Material || a.m_FirstIndex != b.m_FirstIndex ||
					a.m_IndexCount != b.m_IndexCount)
				{
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] bool MatchesMeshSet(const NapaVoxelCpuMeshSet& lhs,
			const NapaVoxelCpuMeshSet& rhs) noexcept
		{
			if (lhs.m_RenderableChunkCount != rhs.m_RenderableChunkCount ||
				lhs.m_VertexCount != rhs.m_VertexCount ||
				lhs.m_IndexCount != rhs.m_IndexCount ||
				lhs.m_SectionCount != rhs.m_SectionCount ||
				lhs.m_Chunks.size() != rhs.m_Chunks.size())
			{
				return false;
			}
			for (std::size_t index = 0; index < lhs.m_Chunks.size(); ++index)
			{
				if (!MatchesChunkMesh(lhs.m_Chunks[index], rhs.m_Chunks[index]))
				{
					return false;
				}
			}
			return true;
		}

		void TestChunkLocalMultiMaterialConversion(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeSingleChunkConfig();
			const std::array primitives{
				MakeSphere(1, VoxelMaterial::Soil, { 5.0, 8.0, 8.0 }, 2.0),
				MakeSphere(2, VoxelMaterial::Stone, { 11.0, 8.0, 8.0 }, 2.0),
			};
			ReferenceWorldMeshingResult meshing{};
			const bool built = BuildWorldMeshes(config, primitives, meshing);
			context.Check(built, "GGLab adapter fixture builds a validated multi-material Core mesh");
			if (!built || meshing.m_Chunks.size() != 1)
			{
				return;
			}

			const ChunkMeshRecord& source = meshing.m_Chunks[0];
			const MeshValidationResult sourceValidation = source.m_Validation;
			NapaVoxelCpuMeshSet converted{};
			const NapaVoxelMeshAdapterResult result =
				ConvertNapaVoxelMeshRecords(meshing.m_Chunks, config, converted);
			const bool countsMatch = result.Succeeded() && converted.m_Chunks.size() == 1 &&
				converted.m_RenderableChunkCount == 1 &&
				converted.m_VertexCount == meshing.m_Validation.m_VertexCount &&
				converted.m_IndexCount == meshing.m_Validation.m_IndexCount &&
				converted.m_SectionCount == meshing.m_Validation.m_SectionCount;
			context.Check(countsMatch,
				"GGLab adapter preserves validated Core vertex, index, section, and Chunk counts");
			if (!countsMatch)
			{
				return;
			}

			const NapaVoxelCpuChunkMesh& chunk = converted.m_Chunks[0];
			context.Check(chunk.m_ChunkOrigin == NapaVoxelWorldPosition{} &&
				MatchesVertices(source.m_Mesh, chunk) &&
				chunk.m_LocalBounds.m_Min.m_X == source.m_Mesh.m_Bounds.m_Min.m_X &&
				chunk.m_LocalBounds.m_Min.m_Y == source.m_Mesh.m_Bounds.m_Min.m_Y &&
				chunk.m_LocalBounds.m_Min.m_Z == source.m_Mesh.m_Bounds.m_Min.m_Z &&
				chunk.m_LocalBounds.m_Max.m_X == source.m_Mesh.m_Bounds.m_Max.m_X &&
				chunk.m_LocalBounds.m_Max.m_Y == source.m_Mesh.m_Bounds.m_Max.m_Y &&
				chunk.m_LocalBounds.m_Max.m_Z == source.m_Mesh.m_Bounds.m_Max.m_Z,
				"GGLab adapter preserves owning Chunk-local positions, normals, and bounds");
			context.Check(source.m_Mesh.m_Sections.size() == 2 &&
				MatchesSectionRanges(source.m_Mesh, chunk),
				"GGLab adapter flattens a legal multi-material mesh into contiguous draw ranges");
			context.Check(source.m_Validation == sourceValidation &&
				chunk.m_CoreValidation == sourceValidation,
				"GGLab conversion preserves the source Core validation evidence");
		}

		void TestEmptyAndNegativeChunkConversion(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig emptyConfig = MakeSingleChunkConfig();
			ReferenceWorldMeshingResult emptyMeshing{};
			NapaVoxelCpuMeshSet emptyConverted{};
			const bool emptyConvertedSuccessfully = BuildWorldMeshes(emptyConfig, {}, emptyMeshing) &&
				ConvertNapaVoxelMeshRecords(
					emptyMeshing.m_Chunks, emptyConfig, emptyConverted).Succeeded();
			context.Check(emptyConvertedSuccessfully && emptyConverted.m_Chunks.size() == 1 &&
				emptyConverted.m_RenderableChunkCount == 0 &&
				emptyConverted.m_Chunks[0].IsEmpty() &&
				emptyConverted.m_Chunks[0].m_Indices.empty() &&
				emptyConverted.m_Chunks[0].m_SectionDrawRanges.empty(),
				"GGLab adapter keeps an Empty Chunk record without renderable mesh data");

			const VoxelWorldConfig negativeConfig = MakeNegativeChunkConfig();
			const std::array negativePrimitives{
				MakeSphere(1, VoxelMaterial::Stone, { -8.0, 8.0, 8.0 }, 2.0),
			};
			ReferenceWorldMeshingResult negativeMeshing{};
			NapaVoxelCpuMeshSet negativeConverted{};
			const bool negativeConvertedSuccessfully =
				BuildWorldMeshes(negativeConfig, negativePrimitives, negativeMeshing) &&
				ConvertNapaVoxelMeshRecords(
					negativeMeshing.m_Chunks, negativeConfig, negativeConverted).Succeeded();
			context.Check(negativeConvertedSuccessfully && negativeConverted.m_Chunks.size() == 1,
				"GGLab adapter converts a validated Negative Chunk mesh");
			if (!negativeConvertedSuccessfully || negativeConverted.m_Chunks.size() != 1)
			{
				return;
			}

			const NapaVoxelCpuChunkMesh& chunk = negativeConverted.m_Chunks[0];
			const bool positionsRemainLocal = std::ranges::all_of(chunk.m_Vertices,
				[](const NapaVoxelRenderVertex& vertex) noexcept
				{
					return vertex.m_Position.m_X >= 0.0f && vertex.m_Position.m_X <= 16.0f;
				});
			Vector3 translation{};
			const NapaVoxelMeshAdapterResult translationResult = ComputeNapaVoxelRenderTranslation(
				negativeConfig, chunk.m_ChunkOrigin, NapaVoxelWorldPosition{}, translation);
			context.Check(chunk.m_Chunk == ChunkCoord{ -1, 0, 0 } &&
				chunk.m_ChunkOrigin == NapaVoxelWorldPosition{ -16.0, 0.0, 0.0 } &&
				positionsRemainLocal && translationResult.Succeeded() &&
				translation.m_X == -16.0f && translation.m_Y == 0.0f && translation.m_Z == 0.0f,
				"GGLab adapter separates a Negative Chunk Double origin from local Float vertices");
		}

		void TestOriginNarrowing(SelfTestContext& context) noexcept
		{
			const napa::voxel::VoxelWorldConfig config = MakeSingleChunkConfig();
			const Vector3 sentinel{ 7.0f, 8.0f, 9.0f };
			Vector3 unchanged = sentinel;
			const NapaVoxelMeshAdapterResult nonFiniteResult = ComputeNapaVoxelRenderTranslation(
				config, {}, { std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0 }, unchanged);
			context.Check(nonFiniteResult.m_Error ==
				NapaVoxelMeshAdapterError::NonFiniteWorldPosition &&
				unchanged.m_X == sentinel.m_X && unchanged.m_Y == sentinel.m_Y &&
				unchanged.m_Z == sentinel.m_Z,
				"GGLab adapter rejects a non-finite Render Origin without modifying output");

			unchanged = sentinel;
			const double unrepresentable =
				static_cast<double>(std::numeric_limits<float>::max()) * 2.0;
			const NapaVoxelMeshAdapterResult rangeResult = ComputeNapaVoxelRenderTranslation(
				config, { unrepresentable, 0.0, 0.0 }, {}, unchanged);
			context.Check(rangeResult.m_Error ==
				NapaVoxelMeshAdapterError::UnrepresentableRenderTranslation &&
				unchanged.m_X == sentinel.m_X && unchanged.m_Y == sentinel.m_Y &&
				unchanged.m_Z == sentinel.m_Z,
				"GGLab adapter rejects an unrepresentable Float translation atomically");

			unchanged = sentinel;
			const NapaVoxelMeshAdapterResult precisionResult = ComputeNapaVoxelRenderTranslation(
				config, { 4096.0, 0.0, 0.0 }, {}, unchanged);
			context.Check(precisionResult.m_Error ==
				NapaVoxelMeshAdapterError::InsufficientRenderTranslationPrecision &&
				unchanged.m_X == sentinel.m_X && unchanged.m_Y == sentinel.m_Y &&
				unchanged.m_Z == sentinel.m_Z,
				"GGLab adapter rejects a finite Float translation that loses mesh precision");

			Vector3 recentered{};
			const NapaVoxelMeshAdapterResult recenteredResult = ComputeNapaVoxelRenderTranslation(
				config, { 4096.0, 0.0, 0.0 }, { 4096.0, 0.0, 0.0 }, recentered);
			context.Check(recenteredResult.Succeeded() && recentered.m_X == 0.0f &&
				recentered.m_Y == 0.0f && recentered.m_Z == 0.0f,
				"GGLab adapter recovers canonical mesh precision after Render Origin recentering");

			napa::voxel::VoxelWorldConfig mixedChunkConfig = MakeSingleChunkConfig();
			mixedChunkConfig.m_LogicalCellBounds = {
				.m_Min = { -32, -16, 16 },
				.m_MaxExclusive = { -16, 0, 32 },
			};
			NapaVoxelWorldPosition mixedChunkOrigin{};
			const NapaVoxelMeshAdapterResult mixedChunkResult = ComputeNapaVoxelChunkOrigin(
				mixedChunkConfig, napa::voxel::ChunkCoord{ -2, -1, 1 }, mixedChunkOrigin);
			context.Check(mixedChunkResult.Succeeded() &&
				mixedChunkOrigin == NapaVoxelWorldPosition{ -32.0, -16.0, 16.0 },
				"GGLab adapter computes a mixed-sign three-axis Chunk origin in Double");
		}

		void TestCanonicalRecordConversion(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeEightChunkConfig();
			const std::array primitives{
				MakeSphere(1, VoxelMaterial::Stone, {}, 2.0),
			};
			ReferenceWorldMeshingResult meshing{};
			const bool built = BuildWorldMeshes(config, primitives, meshing);
			context.Check(built && meshing.m_Chunks.size() == 8,
				"GGLab adapter ordering fixture covers the complete eight-Chunk Domain");
			if (!built || meshing.m_Chunks.size() != 8)
			{
				return;
			}

			std::vector<ChunkMeshRecord> reversed = meshing.m_Chunks;
			std::reverse(reversed.begin(), reversed.end());
			NapaVoxelCpuMeshSet forwardConverted{};
			NapaVoxelCpuMeshSet reverseConverted{};
			const bool converted =
				ConvertNapaVoxelMeshRecords(meshing.m_Chunks, config, forwardConverted).Succeeded() &&
				ConvertNapaVoxelMeshRecords(reversed, config, reverseConverted).Succeeded();
			context.Check(converted && MatchesMeshSet(forwardConverted, reverseConverted),
				"GGLab adapter canonicalizes reverse Chunk input into identical draw order and data");

			std::vector<ChunkMeshRecord> duplicates{
				meshing.m_Chunks[0],
				meshing.m_Chunks[0],
			};
			NapaVoxelCpuMeshSet unchanged{
				.m_Chunks = { NapaVoxelCpuChunkMesh{} },
				.m_RenderableChunkCount = 17,
				.m_VertexCount = 19,
				.m_IndexCount = 23,
				.m_SectionCount = 29,
			};
			const NapaVoxelMeshAdapterResult duplicateResult =
				ConvertNapaVoxelMeshRecords(duplicates, config, unchanged);
			context.Check(duplicateResult.m_Error == NapaVoxelMeshAdapterError::DuplicateChunk &&
				unchanged.m_Chunks.size() == 1 && unchanged.m_RenderableChunkCount == 17 &&
				unchanged.m_VertexCount == 19 && unchanged.m_IndexCount == 23 &&
				unchanged.m_SectionCount == 29,
				"GGLab adapter rejects duplicate Chunks without modifying the destination set");
		}

		void TestInvalidSourceEvidence(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeSingleChunkConfig();
			const std::array primitives{
				MakeSphere(1, VoxelMaterial::Stone, { 8.0, 8.0, 8.0 }, 2.0),
			};
			ReferenceWorldMeshingResult meshing{};
			if (!BuildWorldMeshes(config, primitives, meshing) || meshing.m_Chunks.size() != 1 ||
				meshing.m_Chunks[0].m_Mesh.m_Vertices.empty())
			{
				context.Check(false, "GGLab adapter invalid-evidence fixture builds a non-empty mesh");
				return;
			}

			ChunkMeshRecord nonFinite = meshing.m_Chunks[0];
			nonFinite.m_Mesh.m_Vertices[0].m_Normal.m_X =
				std::numeric_limits<float>::infinity();
			NapaVoxelCpuChunkMesh unchanged{
				.m_SourceWorldVoxelRevision = 37,
			};
			const NapaVoxelMeshAdapterResult nonFiniteResult =
				ConvertNapaVoxelChunkMesh(nonFinite, config, unchanged);
			context.Check(nonFiniteResult.m_Error ==
				NapaVoxelMeshAdapterError::CoreValidationFailed &&
				nonFiniteResult.m_CoreError == ValidationError::NonFiniteMeshVertex &&
				unchanged.m_SourceWorldVoxelRevision == 37,
				"GGLab adapter revalidates Core normals and preserves output on failure");

			ChunkMeshRecord mismatched = meshing.m_Chunks[0];
			++mismatched.m_Validation.m_IndexCount;
			const NapaVoxelMeshAdapterResult mismatchResult =
				ConvertNapaVoxelChunkMesh(mismatched, config, unchanged);
			context.Check(mismatchResult.m_Error ==
				NapaVoxelMeshAdapterError::MismatchedCoreValidation &&
				unchanged.m_SourceWorldVoxelRevision == 37,
				"GGLab adapter rejects stale Core validation evidence atomically");
		}
	}

	void RunNapaVoxelGGLabAdapterSelfTests(SelfTestContext& context) noexcept
	{
		TestChunkLocalMultiMaterialConversion(context);
		TestEmptyAndNegativeChunkConversion(context);
		TestOriginNarrowing(context);
		TestCanonicalRecordConversion(context);
		TestInvalidSourceEvidence(context);
	}
}
