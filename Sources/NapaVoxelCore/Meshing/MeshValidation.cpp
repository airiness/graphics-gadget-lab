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
			Float3 outwardDirection) noexcept
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
			return
				crossX *
					static_cast<double>(outwardDirection.m_X) +
				crossY *
					static_cast<double>(outwardDirection.m_Y) +
				crossZ *
					static_cast<double>(outwardDirection.m_Z) >
				0.0;
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

		CellAabb targetCellBounds{};
		const ValidationResult intersectionResult =
			IntersectCellOwnerChunk(
				chunk,
				config.m_ChunkCellCount,
				config.m_LogicalCellBounds,
				targetCellBounds);
		if (intersectionResult.Failed())
		{
			return intersectionResult;
		}

		const std::int64_t chunkCellCount = config.m_ChunkCellCount;
		const std::int64_t originX =
			static_cast<std::int64_t>(chunk.m_X) *
			chunkCellCount;
		const std::int64_t originY =
			static_cast<std::int64_t>(chunk.m_Y) *
			chunkCellCount;
		const std::int64_t originZ =
			static_cast<std::int64_t>(chunk.m_Z) *
			chunkCellCount;
		const std::int64_t localMinimumX =
			static_cast<std::int64_t>(
				targetCellBounds.m_Min.m_X) -
			originX;
		const std::int64_t localMinimumY =
			static_cast<std::int64_t>(
				targetCellBounds.m_Min.m_Y) -
			originY;
		const std::int64_t localMinimumZ =
			static_cast<std::int64_t>(
				targetCellBounds.m_Min.m_Z) -
			originZ;
		const std::int64_t localMaximumX =
			static_cast<std::int64_t>(
				targetCellBounds.m_MaxExclusive.m_X) -
			originX;
		const std::int64_t localMaximumY =
			static_cast<std::int64_t>(
				targetCellBounds.m_MaxExclusive.m_Y) -
			originY;
		const std::int64_t localMaximumZ =
			static_cast<std::int64_t>(
				targetCellBounds.m_MaxExclusive.m_Z) -
			originZ;

		const std::int64_t quantizationScale =
			static_cast<std::int64_t>(
				MeshPositionQuantizationScale);
		MeshQuantizationContext prepared;
		prepared.m_InverseVoxelSize =
			1.0 / static_cast<double>(config.m_VoxelSize);
		prepared.m_TargetCellDomainMin = {
			static_cast<std::int32_t>(
				localMinimumX * quantizationScale),
			static_cast<std::int32_t>(
				localMinimumY * quantizationScale),
			static_cast<std::int32_t>(
				localMinimumZ * quantizationScale),
		};
		prepared.m_TargetCellDomainMax = {
			static_cast<std::int32_t>(
				localMaximumX * quantizationScale),
			static_cast<std::int32_t>(
				localMaximumY * quantizationScale),
			static_cast<std::int32_t>(
				localMaximumZ * quantizationScale),
		};
		prepared.m_Config = config;
		prepared.m_TargetChunk = chunk;
		prepared.m_IsPrepared = true;
		context = prepared;
		return {};
	}

	bool MeshQuantizationContext::IsPrepared() const noexcept
	{
		return m_IsPrepared;
	}

	bool MeshQuantizationContext::IsCompatible(
		const VoxelWorldConfig& config,
		ChunkCoord chunk) const noexcept
	{
		return
			m_IsPrepared &&
			m_TargetChunk == chunk &&
			m_Config.m_ChunkCellCount == config.m_ChunkCellCount &&
			m_Config.m_VoxelSize == config.m_VoxelSize &&
			m_Config.m_SurfaceBandVoxels ==
				config.m_SurfaceBandVoxels &&
			m_Config.m_LogicalCellBounds ==
				config.m_LogicalCellBounds;
	}

	bool MeshQuantizationContext::ContainsTargetCellDomain(
		QuantizedMeshPosition position) const noexcept
	{
		return
			m_IsPrepared &&
			position.m_X >= m_TargetCellDomainMin.m_X &&
			position.m_Y >= m_TargetCellDomainMin.m_Y &&
			position.m_Z >= m_TargetCellDomainMin.m_Z &&
			position.m_X <= m_TargetCellDomainMax.m_X &&
			position.m_Y <= m_TargetCellDomainMax.m_Y &&
			position.m_Z <= m_TargetCellDomainMax.m_Z;
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
					context.m_InverseVoxelSize,
				prepared.m_X);
		if (xResult.Failed())
		{
			return xResult;
		}
		const ValidationResult yResult =
			QuantizeMeshPositionComponent(
				static_cast<double>(position.m_Y) *
					context.m_InverseVoxelSize,
				prepared.m_Y);
		if (yResult.Failed())
		{
			return yResult;
		}
		const ValidationResult zResult =
			QuantizeMeshPositionComponent(
				static_cast<double>(position.m_Z) *
					context.m_InverseVoxelSize,
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

	ValidationResult ValidateMeshTriangleArea(
		Float3 a,
		Float3 b,
		Float3 c,
		float voxelSize) noexcept
	{
		if (!IsFinite(a) || !IsFinite(b) || !IsFinite(c))
		{
			return { ValidationError::NonFiniteMeshVertex };
		}
		if (!std::isfinite(voxelSize))
		{
			return { ValidationError::NonFiniteVoxelSize };
		}
		if (voxelSize <= 0.0f)
		{
			return { ValidationError::NonPositiveVoxelSize };
		}

		const double doubleAreaSquared =
			ComputeTriangleDoubleAreaSquaredInVoxelUnits(
				a,
				b,
				c,
				1.0 / static_cast<double>(voxelSize));
		if (!std::isfinite(doubleAreaSquared) ||
			doubleAreaSquared <=
				MinimumMeshTriangleDoubleAreaSquared)
		{
			return { ValidationError::DegenerateMeshTriangle };
		}
		return {};
	}

	ValidationResult ValidateAndHashChunkMesh(
		const MeshData& mesh,
		std::span<const MeshTriangleWindingEvidence> windingEvidence,
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
		QuantizedMeshAabb quantizedVertexBounds{};
		bool hasQuantizedVertexBounds = false;
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
			if (!quantizationContext.ContainsTargetCellDomain(
				quantizedVertex.m_Position))
			{
				return {
					ValidationError::
						MeshGeometryOutsideTargetCellDomain,
				};
			}
			const ValidationResult normalResult =
				QuantizeMeshNormal(
					vertex.m_Normal,
					quantizedVertex.m_Normal);
			if (normalResult.Failed())
			{
				return normalResult;
			}
			if (!hasQuantizedVertexBounds)
			{
				quantizedVertexBounds = {
					.m_Min = quantizedVertex.m_Position,
					.m_Max = quantizedVertex.m_Position,
				};
				hasQuantizedVertexBounds = true;
			}
			else
			{
				quantizedVertexBounds.m_Min.m_X = std::min(
					quantizedVertexBounds.m_Min.m_X,
					quantizedVertex.m_Position.m_X);
				quantizedVertexBounds.m_Min.m_Y = std::min(
					quantizedVertexBounds.m_Min.m_Y,
					quantizedVertex.m_Position.m_Y);
				quantizedVertexBounds.m_Min.m_Z = std::min(
					quantizedVertexBounds.m_Min.m_Z,
					quantizedVertex.m_Position.m_Z);
				quantizedVertexBounds.m_Max.m_X = std::max(
					quantizedVertexBounds.m_Max.m_X,
					quantizedVertex.m_Position.m_X);
				quantizedVertexBounds.m_Max.m_Y = std::max(
					quantizedVertexBounds.m_Max.m_Y,
					quantizedVertex.m_Position.m_Y);
				quantizedVertexBounds.m_Max.m_Z = std::max(
					quantizedVertexBounds.m_Max.m_Z,
					quantizedVertex.m_Position.m_Z);
			}
			quantizedVertices.push_back(quantizedVertex);
		}

		std::uint64_t totalIndexCount = 0;
		std::uint64_t triangleCount = 0;
		std::size_t windingEvidenceIndex = 0;
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
				const ValidationResult areaResult =
					ValidateMeshTriangleArea(
						vertexA.m_Position,
						vertexB.m_Position,
						vertexC.m_Position,
						config.m_VoxelSize);
				if (areaResult.Failed())
				{
					return areaResult;
				}

				if (windingEvidenceIndex >= windingEvidence.size())
				{
					return {
						ValidationError::InvalidMeshWindingEvidence,
					};
				}
				const Float3 outwardDirection =
					windingEvidence[
						windingEvidenceIndex++]
						.m_OutwardDirection;
				if (!IsFinite(outwardDirection))
				{
					return {
						ValidationError::InvalidMeshWindingEvidence,
					};
				}
				const double directionX = outwardDirection.m_X;
				const double directionY = outwardDirection.m_Y;
				const double directionZ = outwardDirection.m_Z;
				const double directionLengthSquared =
					directionX * directionX +
					directionY * directionY +
					directionZ * directionZ;
				if (!std::isfinite(directionLengthSquared) ||
					directionLengthSquared <= 0.0 ||
					!HasUnitLength(outwardDirection))
				{
					return {
						ValidationError::InvalidMeshWindingEvidence,
					};
				}

				if (!HasOutwardWinding(
					vertexA.m_Position,
					vertexB.m_Position,
					vertexC.m_Position,
					outwardDirection))
				{
					return { ValidationError::InvalidMeshWinding };
				}
			}
		}
		if (windingEvidenceIndex != windingEvidence.size())
		{
			return {
				ValidationError::InvalidMeshWindingEvidence,
			};
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
			if (quantizedBounds != quantizedVertexBounds)
			{
				return { ValidationError::InvalidMeshBounds };
			}
			if (!quantizationContext.ContainsTargetCellDomain(
					quantizedBounds.m_Min) ||
				!quantizationContext.ContainsTargetCellDomain(
					quantizedBounds.m_Max))
			{
				return {
					ValidationError::
						MeshGeometryOutsideTargetCellDomain,
				};
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
