#include "NapaVoxelCore/Meshing/MeshValidation.h"

#include "NapaVoxelCore/BuildContract.h"
#include "NapaVoxelCore/Field/DensityQuantization.h"
#include "NapaVoxelCore/Hash/CanonicalHash.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		struct QuantizedMeshVertex
		{
			QuantizedMeshPosition m_Position{};
			QuantizedMeshNormal m_Normal{};
		};

		[[nodiscard]] bool IsFinite(Float3 value) noexcept
		{
			return
				std::isfinite(value.m_X) &&
				std::isfinite(value.m_Y) &&
				std::isfinite(value.m_Z);
		}

		[[nodiscard]] bool IsCanonicalEmptyBounds(
			const FloatAabb& bounds) noexcept
		{
			return
				bounds.m_Min == Float3{} &&
				bounds.m_Max == Float3{};
		}

		[[nodiscard]] ValidationResult QuantizeMeshPositionComponent(
			double positionInVoxelUnits,
			std::int32_t& quantized) noexcept
		{
			std::int64_t rounded = 0;
			const ValidationResult roundResult =
				RoundHalfAwayFromZero(
					positionInVoxelUnits *
						MeshPositionQuantizationScale,
					rounded);
			if (roundResult.Failed())
			{
				return { ValidationError::MeshPositionOutOfRange };
			}

			const std::optional<std::int32_t> narrowed =
				CheckedNarrow<std::int32_t>(rounded);
			if (!narrowed)
			{
				return { ValidationError::MeshPositionOutOfRange };
			}

			quantized = *narrowed;
			return {};
		}

		[[nodiscard]] ValidationResult QuantizeMeshNormalComponent(
			float value,
			std::int16_t& quantized) noexcept
		{
			const double clamped = std::clamp(
				static_cast<double>(value),
				-1.0,
				1.0);
			std::int64_t rounded = 0;
			const ValidationResult roundResult =
				RoundHalfAwayFromZero(
					clamped * MeshNormalQuantizationScale,
					rounded);
			if (roundResult.Failed())
			{
				return roundResult;
			}

			const std::optional<std::int16_t> narrowed =
				CheckedNarrow<std::int16_t>(rounded);
			if (!narrowed)
			{
				return { ValidationError::ArithmeticOverflow };
			}

			quantized = *narrowed;
			return {};
		}

		[[nodiscard]] bool HasValidBoundsShape(
			const FloatAabb& bounds) noexcept
		{
			return
				IsFinite(bounds.m_Min) &&
				IsFinite(bounds.m_Max) &&
				bounds.m_Min.m_X <= bounds.m_Max.m_X &&
				bounds.m_Min.m_Y <= bounds.m_Max.m_Y &&
				bounds.m_Min.m_Z <= bounds.m_Max.m_Z;
		}

		[[nodiscard]] bool HasUnitLength(Float3 normal) noexcept
		{
			const double x = static_cast<double>(normal.m_X);
			const double y = static_cast<double>(normal.m_Y);
			const double z = static_cast<double>(normal.m_Z);
			const double length = std::sqrt(x * x + y * y + z * z);
			return
				std::isfinite(length) &&
				std::abs(length - 1.0) <=
					MeshNormalLengthTolerance;
		}

		[[nodiscard]] double ComputeTriangleDoubleAreaSquaredInVoxelUnits(
			Float3 a,
			Float3 b,
			Float3 c,
			double inverseVoxelSize) noexcept
		{
			const double abX =
				(static_cast<double>(b.m_X) -
					static_cast<double>(a.m_X)) *
				inverseVoxelSize;
			const double abY =
				(static_cast<double>(b.m_Y) -
					static_cast<double>(a.m_Y)) *
				inverseVoxelSize;
			const double abZ =
				(static_cast<double>(b.m_Z) -
					static_cast<double>(a.m_Z)) *
				inverseVoxelSize;
			const double acX =
				(static_cast<double>(c.m_X) -
					static_cast<double>(a.m_X)) *
				inverseVoxelSize;
			const double acY =
				(static_cast<double>(c.m_Y) -
					static_cast<double>(a.m_Y)) *
				inverseVoxelSize;
			const double acZ =
				(static_cast<double>(c.m_Z) -
					static_cast<double>(a.m_Z)) *
				inverseVoxelSize;

			const double crossX = abY * acZ - abZ * acY;
			const double crossY = abZ * acX - abX * acZ;
			const double crossZ = abX * acY - abY * acX;
			return
				crossX * crossX +
				crossY * crossY +
				crossZ * crossZ;
		}

		[[nodiscard]] bool HasOutwardWinding(
			Float3 a,
			Float3 b,
			Float3 c,
			Float3 normalA,
			Float3 normalB,
			Float3 normalC) noexcept
		{
			const double abX =
				static_cast<double>(b.m_X) -
				static_cast<double>(a.m_X);
			const double abY =
				static_cast<double>(b.m_Y) -
				static_cast<double>(a.m_Y);
			const double abZ =
				static_cast<double>(b.m_Z) -
				static_cast<double>(a.m_Z);
			const double acX =
				static_cast<double>(c.m_X) -
				static_cast<double>(a.m_X);
			const double acY =
				static_cast<double>(c.m_Y) -
				static_cast<double>(a.m_Y);
			const double acZ =
				static_cast<double>(c.m_Z) -
				static_cast<double>(a.m_Z);

			const double crossX = abY * acZ - abZ * acY;
			const double crossY = abZ * acX - abX * acZ;
			const double crossZ = abX * acY - abY * acX;
			const double normalX =
				static_cast<double>(normalA.m_X) +
				static_cast<double>(normalB.m_X) +
				static_cast<double>(normalC.m_X);
			const double normalY =
				static_cast<double>(normalA.m_Y) +
				static_cast<double>(normalB.m_Y) +
				static_cast<double>(normalC.m_Y);
			const double normalZ =
				static_cast<double>(normalA.m_Z) +
				static_cast<double>(normalB.m_Z) +
				static_cast<double>(normalC.m_Z);
			return
				crossX * normalX +
				crossY * normalY +
				crossZ * normalZ > 0.0;
		}

		void WriteConfig(
			CanonicalHashWriter& writer,
			const VoxelWorldConfig& config) noexcept
		{
			const CellAabb& bounds = config.m_LogicalCellBounds;
			writer.WriteI32(bounds.m_Min.m_X);
			writer.WriteI32(bounds.m_Min.m_Y);
			writer.WriteI32(bounds.m_Min.m_Z);
			writer.WriteI32(bounds.m_MaxExclusive.m_X);
			writer.WriteI32(bounds.m_MaxExclusive.m_Y);
			writer.WriteI32(bounds.m_MaxExclusive.m_Z);
			writer.WriteU32(config.m_ChunkCellCount);
			writer.WriteFloat32(config.m_VoxelSize);
			writer.WriteU8(IsoValue);
			writer.WriteFloat32(config.m_SurfaceBandVoxels);
		}

		void WriteQuantizedPosition(
			CanonicalHashWriter& writer,
			QuantizedMeshPosition position) noexcept
		{
			writer.WriteI32(position.m_X);
			writer.WriteI32(position.m_Y);
			writer.WriteI32(position.m_Z);
		}

		void WriteQuantizedNormal(
			CanonicalHashWriter& writer,
			QuantizedMeshNormal normal) noexcept
		{
			writer.WriteI16(normal.m_X);
			writer.WriteI16(normal.m_Y);
			writer.WriteI16(normal.m_Z);
		}
	}

	ValidationResult PrepareMeshQuantizationContext(
		const VoxelWorldConfig& config,
		ChunkCoord chunk,
		MeshQuantizationContext& context) noexcept
	{
		const ValidationResult configResult = ValidateConfig(config);
		if (configResult.Failed())
		{
			return configResult;
		}
		LogicalDomainMetrics metrics{};
		const ValidationResult metricsResult =
			ComputeLogicalDomainMetrics(config, metrics);
		if (metricsResult.Failed())
		{
			return metricsResult;
		}
		if (!metrics.m_CellOwnerChunkBounds.Contains(chunk))
		{
			return {
				ValidationError::ChunkOutsideLogicalCellDomain,
			};
		}

		MeshQuantizationContext prepared;
		prepared.m_InverseVoxelSize =
			1.0 / static_cast<double>(config.m_VoxelSize);
		prepared.m_ChunkOriginVoxelX =
			static_cast<double>(chunk.m_X) *
			static_cast<double>(config.m_ChunkCellCount);
		prepared.m_ChunkOriginVoxelY =
			static_cast<double>(chunk.m_Y) *
			static_cast<double>(config.m_ChunkCellCount);
		prepared.m_ChunkOriginVoxelZ =
			static_cast<double>(chunk.m_Z) *
			static_cast<double>(config.m_ChunkCellCount);
		prepared.m_IsPrepared = true;
		context = prepared;
		return {};
	}

	ValidationResult QuantizeMeshPosition(
		Float3 position,
		const MeshQuantizationContext& context,
		QuantizedMeshPosition& quantized) noexcept
	{
		if (!context.m_IsPrepared)
		{
			return {
				ValidationError::UnpreparedMeshQuantizationContext,
			};
		}
		if (!IsFinite(position))
		{
			return { ValidationError::NonFiniteMeshVertex };
		}

		QuantizedMeshPosition prepared{};
		const ValidationResult xResult =
			QuantizeMeshPositionComponent(
				static_cast<double>(position.m_X) *
					context.m_InverseVoxelSize -
					context.m_ChunkOriginVoxelX,
				prepared.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult =
			QuantizeMeshPositionComponent(
				static_cast<double>(position.m_Y) *
					context.m_InverseVoxelSize -
					context.m_ChunkOriginVoxelY,
				prepared.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult =
			QuantizeMeshPositionComponent(
				static_cast<double>(position.m_Z) *
					context.m_InverseVoxelSize -
					context.m_ChunkOriginVoxelZ,
				prepared.m_Z);
		if (zResult.Failed())
		{
			return zResult;
		}

		quantized = prepared;
		return {};
	}

	ValidationResult QuantizeMeshNormal(
		Float3 normal,
		QuantizedMeshNormal& quantized) noexcept
	{
		if (!IsFinite(normal))
		{
			return { ValidationError::NonFiniteMeshVertex };
		}

		QuantizedMeshNormal prepared{};
		const ValidationResult xResult =
			QuantizeMeshNormalComponent(
				normal.m_X,
				prepared.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult =
			QuantizeMeshNormalComponent(
				normal.m_Y,
				prepared.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult =
			QuantizeMeshNormalComponent(
				normal.m_Z,
				prepared.m_Z);
		if (zResult.Failed())
		{
			return zResult;
		}

		quantized = prepared;
		return {};
	}

	ValidationResult ValidateAndHashChunkMesh(
		const MeshData& mesh,
		const VoxelWorldConfig& config,
		ChunkCoord chunk,
		MeshValidationResult& result)
	{
		MeshQuantizationContext quantizationContext;
		const ValidationResult contextResult =
			PrepareMeshQuantizationContext(
				config,
				chunk,
				quantizationContext);
		if (contextResult.Failed())
		{
			return contextResult;
		}
		const double inverseVoxelSize =
			1.0 / static_cast<double>(config.m_VoxelSize);

		if (!HasValidBoundsShape(mesh.m_Bounds))
		{
			return { ValidationError::InvalidMeshBounds };
		}
		if (mesh.m_Vertices.empty())
		{
			if (!mesh.m_Sections.empty())
			{
				return { ValidationError::InvalidMeshSection };
			}
			if (!IsCanonicalEmptyBounds(mesh.m_Bounds))
			{
				return { ValidationError::InvalidMeshBounds };
			}
		}
		else if (mesh.m_Sections.empty())
		{
			return { ValidationError::InvalidMeshSection };
		}

		std::vector<QuantizedMeshVertex> quantizedVertices;
		quantizedVertices.reserve(mesh.m_Vertices.size());
		for (const MeshVertex& vertex : mesh.m_Vertices)
		{
			if (!IsFinite(vertex.m_Position) ||
				!IsFinite(vertex.m_Normal))
			{
				return { ValidationError::NonFiniteMeshVertex };
			}
			if (!mesh.m_Bounds.Contains(vertex.m_Position))
			{
				return { ValidationError::InvalidMeshBounds };
			}
			if (!HasUnitLength(vertex.m_Normal))
			{
				return { ValidationError::InvalidMeshNormal };
			}

			QuantizedMeshVertex quantizedVertex{};
			const ValidationResult positionResult =
				QuantizeMeshPosition(
					vertex.m_Position,
					quantizationContext,
					quantizedVertex.m_Position);
			if (positionResult.Failed())
			{
				return positionResult;
			}
			const ValidationResult normalResult =
				QuantizeMeshNormal(
					vertex.m_Normal,
					quantizedVertex.m_Normal);
			if (normalResult.Failed())
			{
				return normalResult;
			}
			quantizedVertices.push_back(quantizedVertex);
		}

		std::uint64_t totalIndexCount = 0;
		std::uint64_t triangleCount = 0;
		std::uint8_t previousMaterial = 0;
		bool hasPreviousMaterial = false;
		for (const MeshSection& section : mesh.m_Sections)
		{
			if (!IsKnownVoxelMaterial(section.m_Material) ||
				section.m_Material == VoxelMaterial::Empty)
			{
				return { ValidationError::InvalidMeshSection };
			}

			const std::uint8_t material =
				static_cast<std::uint8_t>(section.m_Material);
			if (hasPreviousMaterial &&
				material <= previousMaterial)
			{
				return { ValidationError::InvalidMeshSection };
			}
			previousMaterial = material;
			hasPreviousMaterial = true;

			if (section.m_Indices.empty() ||
				section.m_Indices.size() % 3 != 0)
			{
				return { ValidationError::InvalidMeshIndexCount };
			}

			const std::optional<std::uint64_t> sectionIndexCount =
				CheckedNarrow<std::uint64_t>(
					section.m_Indices.size());
			if (!sectionIndexCount)
			{
				return { ValidationError::ArithmeticOverflow };
			}
			const std::optional<std::uint64_t> nextTotal =
				CheckedAdd(totalIndexCount, *sectionIndexCount);
			if (!nextTotal)
			{
				return { ValidationError::ArithmeticOverflow };
			}
			totalIndexCount = *nextTotal;
			triangleCount += *sectionIndexCount / 3;

			for (std::size_t indexOffset = 0;
				indexOffset < section.m_Indices.size();
				indexOffset += 3)
			{
				const std::uint32_t indexA =
					section.m_Indices[indexOffset];
				const std::uint32_t indexB =
					section.m_Indices[indexOffset + 1];
				const std::uint32_t indexC =
					section.m_Indices[indexOffset + 2];
				if (indexA >= mesh.m_Vertices.size() ||
					indexB >= mesh.m_Vertices.size() ||
					indexC >= mesh.m_Vertices.size())
				{
					return { ValidationError::MeshIndexOutOfRange };
				}

				const QuantizedMeshPosition positionA =
					quantizedVertices[indexA].m_Position;
				const QuantizedMeshPosition positionB =
					quantizedVertices[indexB].m_Position;
				const QuantizedMeshPosition positionC =
					quantizedVertices[indexC].m_Position;
				if (positionA == positionB ||
					positionA == positionC ||
					positionB == positionC)
				{
					return { ValidationError::DegenerateMeshTriangle };
				}

				const MeshVertex& vertexA = mesh.m_Vertices[indexA];
				const MeshVertex& vertexB = mesh.m_Vertices[indexB];
				const MeshVertex& vertexC = mesh.m_Vertices[indexC];
				const double doubleAreaSquared =
					ComputeTriangleDoubleAreaSquaredInVoxelUnits(
						vertexA.m_Position,
						vertexB.m_Position,
						vertexC.m_Position,
						inverseVoxelSize);
				if (!std::isfinite(doubleAreaSquared) ||
					doubleAreaSquared <=
						MinimumMeshTriangleDoubleAreaSquared)
				{
					return { ValidationError::DegenerateMeshTriangle };
				}

				if (!HasOutwardWinding(
					vertexA.m_Position,
					vertexB.m_Position,
					vertexC.m_Position,
					vertexA.m_Normal,
					vertexB.m_Normal,
					vertexC.m_Normal))
				{
					return { ValidationError::InvalidMeshWinding };
				}
			}
		}

		QuantizedMeshAabb quantizedBounds{};
		if (!mesh.m_Vertices.empty())
		{
			const ValidationResult minimumResult =
				QuantizeMeshPosition(
					mesh.m_Bounds.m_Min,
					quantizationContext,
					quantizedBounds.m_Min);
			if (minimumResult.Failed())
			{
				return minimumResult;
			}
			const ValidationResult maximumResult =
				QuantizeMeshPosition(
					mesh.m_Bounds.m_Max,
					quantizationContext,
					quantizedBounds.m_Max);
			if (maximumResult.Failed())
			{
				return maximumResult;
			}
		}

		const std::optional<std::uint64_t> vertexCount =
			CheckedNarrow<std::uint64_t>(mesh.m_Vertices.size());
		const std::optional<std::uint64_t> sectionCount =
			CheckedNarrow<std::uint64_t>(mesh.m_Sections.size());
		if (!vertexCount || !sectionCount)
		{
			return { ValidationError::ArithmeticOverflow };
		}

		CanonicalHashWriter writer;
		const BuildContract& contract = GetBuildContract();
		writer.WriteU32(contract.m_MeshHashSchemaVersion);
		writer.WriteU32(contract.m_ReferenceMesherVersion);
		WriteConfig(writer, config);
		writer.WriteI32(chunk.m_X);
		writer.WriteI32(chunk.m_Y);
		writer.WriteI32(chunk.m_Z);
		writer.WriteCount(*vertexCount);
		writer.WriteCount(*sectionCount);
		writer.WriteCount(totalIndexCount);

		for (const QuantizedMeshVertex& vertex : quantizedVertices)
		{
			WriteQuantizedPosition(writer, vertex.m_Position);
			WriteQuantizedNormal(writer, vertex.m_Normal);
		}
		for (const MeshSection& section : mesh.m_Sections)
		{
			writer.WriteEnum(section.m_Material);
			writer.WriteCount(
				static_cast<std::uint64_t>(
					section.m_Indices.size()));
			for (const std::uint32_t index : section.m_Indices)
			{
				writer.WriteU32(index);
			}
		}
		WriteQuantizedPosition(writer, quantizedBounds.m_Min);
		WriteQuantizedPosition(writer, quantizedBounds.m_Max);

		MeshValidationResult validated{
			.m_ValidationHash = writer.GetValue(),
			.m_VertexCount = *vertexCount,
			.m_SectionCount = *sectionCount,
			.m_IndexCount = totalIndexCount,
			.m_TriangleCount = triangleCount,
			.m_QuantizedBounds = quantizedBounds,
		};
		result = validated;
		return {};
	}
}
