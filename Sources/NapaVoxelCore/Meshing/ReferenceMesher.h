#pragma once

#include "NapaVoxelCore/Meshing/MeshData.h"
#include "NapaVoxelCore/Validation/ValidationResult.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <array>
#include <cstdint>

namespace napa::voxel
{
	inline constexpr std::array<CellCornerOffset, 8>
		ReferenceCubeCornerOffsets{
			CellCornerOffset{ 0, 0, 0 },
			CellCornerOffset{ 1, 0, 0 },
			CellCornerOffset{ 0, 1, 0 },
			CellCornerOffset{ 1, 1, 0 },
			CellCornerOffset{ 0, 0, 1 },
			CellCornerOffset{ 1, 0, 1 },
			CellCornerOffset{ 0, 1, 1 },
			CellCornerOffset{ 1, 1, 1 },
		};

	inline constexpr std::array<
		std::array<std::uint8_t, 4>,
		6> ReferenceFreudenthalTetrahedra{
			std::array<std::uint8_t, 4>{ 0, 1, 3, 7 },
			std::array<std::uint8_t, 4>{ 0, 3, 2, 7 },
			std::array<std::uint8_t, 4>{ 0, 2, 6, 7 },
			std::array<std::uint8_t, 4>{ 0, 6, 4, 7 },
			std::array<std::uint8_t, 4>{ 0, 4, 5, 7 },
			std::array<std::uint8_t, 4>{ 0, 5, 1, 7 },
		};

	inline constexpr std::array<
		std::array<std::uint8_t, 2>,
		6> ReferenceTetrahedronEdges{
			std::array<std::uint8_t, 2>{ 0, 1 },
			std::array<std::uint8_t, 2>{ 0, 2 },
			std::array<std::uint8_t, 2>{ 0, 3 },
			std::array<std::uint8_t, 2>{ 1, 2 },
			std::array<std::uint8_t, 2>{ 1, 3 },
			std::array<std::uint8_t, 2>{ 2, 3 },
		};

	struct DensityGradient
	{
		double m_X = 0.0;
		double m_Y = 0.0;
		double m_Z = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const DensityGradient&,
			const DensityGradient&) noexcept = default;
	};

	struct ReferenceEdgeEndpoint
	{
		SampleCoord m_Coordinate{};
		VoxelSample m_Sample{};
		DensityGradient m_DensityGradient{};

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceEdgeEndpoint&,
			const ReferenceEdgeEndpoint&) noexcept = default;
	};

	struct ReferenceEdgeVertex
	{
		Float3 m_Position{};
		Float3 m_Normal{};
		DensityGradient m_DensityGradient{};
		SampleCoord m_EndpointA{};
		SampleCoord m_EndpointB{};
		double m_InterpolationT = 0.0;

		[[nodiscard]] friend constexpr bool operator==(
			const ReferenceEdgeVertex&,
			const ReferenceEdgeVertex&) noexcept = default;
	};

	class ReferenceMesher final
	{
	public:
		explicit ReferenceMesher(const VoxelWorld& world) noexcept;

		[[nodiscard]] ValidationResult ComputeSampleDensityGradient(
			SampleCoord coordinate,
			DensityGradient& gradient) const noexcept;
		[[nodiscard]] ValidationResult InterpolateEdge(
			ReferenceEdgeEndpoint first,
			ReferenceEdgeEndpoint second,
			ReferenceEdgeVertex& vertex) const noexcept;

	private:
		const VoxelWorld& m_World;
	};
}
