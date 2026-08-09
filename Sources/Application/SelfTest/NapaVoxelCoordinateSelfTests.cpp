#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Math/Vector.h"
#include "NapaVoxelCore/Validation/CheckedArithmetic.h"
#include "NapaVoxelCore/World/Coordinates.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig MakeValidConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 16,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -16, -8, -4 },
					.m_MaxExclusive = { 16, 8, 4 },
				},
			};
		}

		void RunDouble3MathTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;
			constexpr Double3 lhs{ 1.0, 2.0, 3.0 };
			constexpr Double3 rhs{ -2.0, 4.0, 0.5 };
			context.Check(lhs - rhs == Double3{ 3.0, -2.0, 2.5 } &&
				Dot(lhs, rhs) == 7.5 && Cross(lhs, rhs) == Double3{ -11.0, -6.5, 8.0 },
				"Double3 arithmetic uses one portable Core Math contract");

			context.Check(IsFinite(lhs) && !IsFinite({
				std::numeric_limits<double>::infinity(), 0.0, 0.0,
				}), "Double3 finite validation covers every component");

			Double3 normalized{};
			const double maximum = std::numeric_limits<double>::max();
			const bool normalizedMaximum = TryNormalize({ maximum, maximum, 0.0 }, normalized);
			context.Check(normalizedMaximum &&
				std::abs(Dot(normalized, normalized) - 1.0) <= 1.0e-15 &&
				normalized.m_X == normalized.m_Y && normalized.m_Z == 0.0,
				"Double3 normalization scales finite maximum inputs without overflow");

			const Double3 sentinel{ 91.0, 92.0, 93.0 };
			Double3 unchanged = sentinel;
			context.Check(!TryNormalize({}, unchanged) && unchanged == sentinel &&
				!TryNormalize({ std::numeric_limits<double>::quiet_NaN(), 0.0, 1.0 },
					unchanged) && unchanged == sentinel,
				"Double3 normalization rejects invalid inputs without changing output");
		}

		void CheckValidationError(
			SelfTestContext& context,
			const napa::voxel::VoxelWorldConfig& config,
			napa::voxel::ValidationError expectedError,
			std::string_view name) noexcept
		{
			const napa::voxel::ValidationResult result =
				napa::voxel::ValidateConfig(config);
			context.Check(result.m_Error == expectedError, name);
		}

		void RunCheckedArithmeticTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			constexpr std::int32_t Int32Min =
				std::numeric_limits<std::int32_t>::min();
			constexpr std::int32_t Int32Max =
				std::numeric_limits<std::int32_t>::max();

			const std::optional<std::int32_t> add =
				CheckedAdd(std::int32_t{ -20 }, std::int32_t{ 12 });
			context.Check(add && *add == -8, "CheckedAdd returns an exact signed sum");
			context.Check(
				!CheckedAdd(Int32Max, std::int32_t{ 1 }),
				"CheckedAdd rejects positive signed overflow");
			context.Check(
				!CheckedAdd(Int32Min, std::int32_t{ -1 }),
				"CheckedAdd rejects negative signed overflow");
			context.Check(
				!CheckedAdd(
					std::numeric_limits<std::uint32_t>::max(),
					std::uint32_t{ 1 }),
				"CheckedAdd rejects unsigned overflow");

			const std::optional<std::int32_t> multiply =
				CheckedMul(std::int32_t{ -32 }, std::int32_t{ 16 });
			context.Check(
				multiply && *multiply == -512,
				"CheckedMul returns an exact signed product");
			context.Check(
				!CheckedMul(Int32Max, std::int32_t{ 2 }),
				"CheckedMul rejects positive signed overflow");
			context.Check(
				!CheckedMul(Int32Min, std::int32_t{ -1 }),
				"CheckedMul rejects the signed minimum negation");
			context.Check(
				!CheckedMul(
					std::numeric_limits<std::size_t>::max(),
					std::size_t{ 2 }),
				"CheckedMul rejects size capacity overflow");

			const std::optional<std::int32_t> narrowMaximum =
				CheckedNarrow<std::int32_t>(
					static_cast<std::int64_t>(Int32Max));
			context.Check(
				narrowMaximum && *narrowMaximum == Int32Max,
				"CheckedNarrow accepts the signed maximum");

			const std::int64_t aboveInt32 =
				static_cast<std::int64_t>(Int32Max) + 1;
			const std::int64_t belowInt32 =
				static_cast<std::int64_t>(Int32Min) - 1;
			context.Check(
				!CheckedNarrow<std::int32_t>(aboveInt32),
				"CheckedNarrow rejects a value above the target maximum");
			context.Check(
				!CheckedNarrow<std::int32_t>(belowInt32),
				"CheckedNarrow rejects a value below the target minimum");
			context.Check(
				!CheckedNarrow<std::uint32_t>(std::int64_t{ -1 }),
				"CheckedNarrow rejects a negative unsigned value");

			constexpr std::array<std::int32_t, 9> BoundaryValues{
				Int32Min,
				Int32Min + 1,
				-2,
				-1,
				0,
				1,
				2,
				Int32Max - 1,
				Int32Max,
			};
			bool addMatrixMatches = true;
			bool multiplyMatrixMatches = true;
			for (const std::int32_t lhs : BoundaryValues)
			{
				for (const std::int32_t rhs : BoundaryValues)
				{
					const std::int64_t wideAdd =
						static_cast<std::int64_t>(lhs) + rhs;
					const bool addIsRepresentable =
						wideAdd >= Int32Min && wideAdd <= Int32Max;
					const std::optional<std::int32_t> checkedAdd =
						CheckedAdd(lhs, rhs);
					addMatrixMatches &=
						checkedAdd.has_value() == addIsRepresentable &&
						(!checkedAdd || *checkedAdd == wideAdd);

					const std::int64_t wideMultiply =
						static_cast<std::int64_t>(lhs) * rhs;
					const bool multiplyIsRepresentable =
						wideMultiply >= Int32Min && wideMultiply <= Int32Max;
					const std::optional<std::int32_t> checkedMultiply =
						CheckedMul(lhs, rhs);
					multiplyMatrixMatches &=
						checkedMultiply.has_value() == multiplyIsRepresentable &&
						(!checkedMultiply || *checkedMultiply == wideMultiply);
				}
			}
			context.Check(
				addMatrixMatches,
				"CheckedAdd matches wide arithmetic across signed boundaries");
			context.Check(
				multiplyMatrixMatches,
				"CheckedMul matches wide arithmetic across signed boundaries");
		}

		void RunFloorDivisionTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			struct DivisionCase
			{
				std::int32_t m_Value = 0;
				std::int32_t m_ExpectedQuotient = 0;
				std::uint32_t m_ExpectedRemainder = 0;
				std::string_view m_Name;
			};

			constexpr std::array<DivisionCase, 9> Cases{
				DivisionCase{ 17, 1, 1, "Floor division handles a value above a positive boundary" },
				DivisionCase{ 16, 1, 0, "Floor division handles a positive boundary" },
				DivisionCase{ 15, 0, 15, "Floor division handles a positive interior" },
				DivisionCase{ 0, 0, 0, "Floor division handles zero" },
				DivisionCase{ -1, -1, 15, "Floor division handles negative one" },
				DivisionCase{ -15, -1, 1, "Floor division handles a negative interior" },
				DivisionCase{ -16, -1, 0, "Floor division handles a negative boundary" },
				DivisionCase{ -17, -2, 15, "Floor division handles a value below a negative boundary" },
				DivisionCase{
					std::numeric_limits<std::int32_t>::min(),
					-134217728,
					0,
					"Floor division handles int32 minimum",
				},
			};

			for (const DivisionCase& divisionCase : Cases)
			{
				const std::optional<std::int32_t> quotient =
					FloorDiv(divisionCase.m_Value, 16);
				const std::optional<std::uint32_t> remainder =
					FloorMod(divisionCase.m_Value, 16);
				context.Check(
					quotient &&
					remainder &&
					*quotient == divisionCase.m_ExpectedQuotient &&
					*remainder == divisionCase.m_ExpectedRemainder,
					divisionCase.m_Name);
			}
			context.Check(
				!FloorDiv(1, 0) && !FloorMod(1, 0),
				"FloorDiv and FloorMod reject a zero divisor");

			for (const std::uint32_t chunkCellCount : { 8u, 16u, 32u })
			{
				bool identityHolds = true;
				const std::int32_t range =
					static_cast<std::int32_t>(chunkCellCount * 2);
				for (std::int32_t value = -range; value <= range; ++value)
				{
					const std::optional<std::int32_t> quotient =
						FloorDiv(value, chunkCellCount);
					const std::optional<std::uint32_t> remainder =
						FloorMod(value, chunkCellCount);
					identityHolds &=
						quotient &&
						remainder &&
						*remainder < chunkCellCount &&
						static_cast<std::int64_t>(*quotient) *
							chunkCellCount +
							*remainder == value;
				}
				context.Check(
					identityHolds,
					"Floor division reconstructs coordinates for a supported N");
			}
		}

		void RunCoordinateOwnershipTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			struct OwnerCase
			{
				SampleCoord m_Global{};
				OwnedSampleAddress m_Expected{};
			};

			constexpr std::array<OwnerCase, 8> Cases{
				OwnerCase{ { 15, 0, 0 }, { { 0, 0, 0 }, { 15, 0, 0 } } },
				OwnerCase{ { 16, 0, 0 }, { { 1, 0, 0 }, { 0, 0, 0 } } },
				OwnerCase{ { 17, 0, 0 }, { { 1, 0, 0 }, { 1, 0, 0 } } },
				OwnerCase{ { -1, 0, 0 }, { { -1, 0, 0 }, { 15, 0, 0 } } },
				OwnerCase{ { -16, 0, 0 }, { { -1, 0, 0 }, { 0, 0, 0 } } },
				OwnerCase{ { -17, 0, 0 }, { { -2, 0, 0 }, { 15, 0, 0 } } },
				OwnerCase{
					{ -1, 16, 32 },
					{ { -1, 1, 2 }, { 15, 0, 0 } },
				},
				OwnerCase{
					{ -17, -16, -15 },
					{ { -2, -1, -1 }, { 15, 0, 1 } },
				},
			};

			bool explicitCasesMatch = true;
			for (const OwnerCase& ownerCase : Cases)
			{
				OwnedSampleAddress address{};
				explicitCasesMatch &=
					ResolveSampleOwner(
						ownerCase.m_Global,
						16,
						address).Succeeded() &&
					address == ownerCase.m_Expected;
			}
			context.Check(
				explicitCasesMatch,
				"Sample owner resolution handles faces, edges, corners, and negatives");

			OwnedCellAddress cellAddress{};
			context.Check(
				ResolveCellOwner({ -1, 16, 32 }, 16, cellAddress).Succeeded() &&
				cellAddress ==
					OwnedCellAddress{ { -1, 1, 2 }, { 15, 0, 0 } },
				"Cell owner resolution uses the same canonical ownership rule");

			OwnedSampleAddress unchangedAddress{
				.m_Owner = { 7, 8, 9 },
				.m_Local = { 1, 2, 3 },
			};
			const OwnedSampleAddress expectedUnchangedAddress = unchangedAddress;
			context.Check(
				ResolveSampleOwner({ 0, 0, 0 }, 7, unchangedAddress).m_Error ==
					ValidationError::InvalidChunkCellCount &&
				unchangedAddress == expectedUnchangedAddress,
				"Failed owner resolution leaves the output address unchanged");

			for (const std::uint32_t chunkCellCount : { 8u, 16u, 32u })
			{
				bool roundTripMatches = true;
				const std::int32_t n =
					static_cast<std::int32_t>(chunkCellCount);
				for (std::int32_t z = -n - 1; z <= n + 1; ++z)
				{
					for (std::int32_t y = -n - 1; y <= n + 1; ++y)
					{
						for (std::int32_t x = -n - 1; x <= n + 1; ++x)
						{
							const SampleCoord sample{ x, y, z };
							const CellCoord cell{ x, y, z };
							OwnedSampleAddress sampleAddress{};
							OwnedCellAddress cellAddress{};
							SampleCoord reconstructedSample{};
							CellCoord reconstructedCell{};
							roundTripMatches &=
								ResolveSampleOwner(
									sample,
									chunkCellCount,
									sampleAddress).Succeeded() &&
								sampleAddress.m_Local.m_X < chunkCellCount &&
								sampleAddress.m_Local.m_Y < chunkCellCount &&
								sampleAddress.m_Local.m_Z < chunkCellCount &&
								ChunkLocalToGlobalSample(
									sampleAddress.m_Owner,
									sampleAddress.m_Local,
									chunkCellCount,
									reconstructedSample).Succeeded() &&
								reconstructedSample == sample &&
								ResolveCellOwner(
									cell,
									chunkCellCount,
									cellAddress).Succeeded() &&
								cellAddress.m_Local.m_X < chunkCellCount &&
								cellAddress.m_Local.m_Y < chunkCellCount &&
								cellAddress.m_Local.m_Z < chunkCellCount &&
								ChunkLocalToGlobalCell(
									cellAddress.m_Owner,
									cellAddress.m_Local,
									chunkCellCount,
									reconstructedCell).Succeeded() &&
								reconstructedCell == cell;
						}
					}
				}
				context.Check(
					roundTripMatches,
					"Sample and cell ownership are unique and reversible for a supported N");
			}
		}

		void RunCanonicalOrderingTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::array<ChunkCoord, 7> chunks{
				ChunkCoord{ 1, 0, 0 },
				ChunkCoord{ 0, 0, 1 },
				ChunkCoord{ 0, 1, 0 },
				ChunkCoord{ -1, 0, 0 },
				ChunkCoord{ 0, -1, 0 },
				ChunkCoord{ 0, 0, -1 },
				ChunkCoord{ 0, 0, 0 },
			};
			std::ranges::sort(chunks, ChunkCoordZYXLess{});

			constexpr std::array<ChunkCoord, 7> Expected{
				ChunkCoord{ 0, 0, -1 },
				ChunkCoord{ 0, -1, 0 },
				ChunkCoord{ -1, 0, 0 },
				ChunkCoord{ 0, 0, 0 },
				ChunkCoord{ 1, 0, 0 },
				ChunkCoord{ 0, 1, 0 },
				ChunkCoord{ 0, 0, 1 },
			};
			context.Check(
				chunks == Expected,
				"ChunkCoordZYXLess orders chunks by Z, then Y, then X");
		}

		void RunFlattenTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			for (const std::uint32_t chunkCellCount : { 8u, 16u, 32u })
			{
				bool roundTripMatches = true;
				for (std::uint32_t z = 0; z < chunkCellCount; ++z)
				{
					for (std::uint32_t y = 0; y < chunkCellCount; ++y)
					{
						for (std::uint32_t x = 0; x < chunkCellCount; ++x)
						{
							const LocalCoord expected{ x, y, z };
							std::size_t flatIndex = 0;
							LocalCoord actual{};
							roundTripMatches &=
								FlattenLocal(
									expected,
									chunkCellCount,
									flatIndex).Succeeded() &&
								flatIndex ==
									x + chunkCellCount *
										(y + chunkCellCount * z) &&
								UnflattenLocal(
									flatIndex,
									chunkCellCount,
									actual).Succeeded() &&
								actual == expected;
						}
					}
				}
				context.Check(
					roundTripMatches,
					"FlattenLocal and UnflattenLocal round-trip a supported chunk");
			}

			std::size_t flatIndex = 123;
			context.Check(
				FlattenLocal({ 16, 0, 0 }, 16, flatIndex).m_Error ==
					ValidationError::InvalidLocalCoordinate &&
				flatIndex == 123,
				"FlattenLocal rejects LocalCoord components equal to N");

			LocalCoord local{ 1, 2, 3 };
			context.Check(
				UnflattenLocal(4096, 16, local).m_Error ==
					ValidationError::FlatIndexOutOfRange &&
				local == LocalCoord{ 1, 2, 3 },
				"UnflattenLocal rejects the first index beyond chunk capacity");
			context.Check(
				ValidateLocalCoord({ 0, 0, 0 }, 7).m_Error ==
					ValidationError::InvalidChunkCellCount &&
				UnflattenLocal(0, 7, local).m_Error ==
					ValidationError::InvalidChunkCellCount,
				"Local coordinate operations reject unsupported chunk sizes");
		}

		void RunCellCornerAndHaloTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			bool cornersMatch = true;
			for (std::uint32_t z = 0; z <= 1; ++z)
			{
				for (std::uint32_t y = 0; y <= 1; ++y)
				{
					for (std::uint32_t x = 0; x <= 1; ++x)
					{
						SampleCoord sample{};
						cornersMatch &=
							CellCornerToGlobalSample(
								{ -5, 7, 11 },
								{ x, y, z },
								sample).Succeeded() &&
							sample == SampleCoord{
								-5 + static_cast<std::int32_t>(x),
								7 + static_cast<std::int32_t>(y),
								11 + static_cast<std::int32_t>(z),
							};
					}
				}
			}
			context.Check(
				cornersMatch,
				"CellCornerToGlobalSample accepts all eight cell corners");

			SampleCoord sample{ 1, 2, 3 };
			context.Check(
				CellCornerToGlobalSample(
					{ 0, 0, 0 },
					{ 2, 0, 0 },
					sample).m_Error ==
					ValidationError::InvalidCellCornerOffset &&
				sample == SampleCoord{ 1, 2, 3 },
				"CellCornerToGlobalSample rejects offsets outside zero and one");
			context.Check(
				CellCornerToGlobalSample(
					{ std::numeric_limits<std::int32_t>::max(), 0, 0 },
					{ 1, 0, 0 },
					sample).m_Error ==
					ValidationError::CoordinateOutOfRange,
				"CellCornerToGlobalSample rejects coordinate overflow");

			struct HaloCase
			{
				CellCoord m_Cell{};
				CellCornerOffset m_Corner{};
				OwnedSampleAddress m_Expected{};
			};
			constexpr std::array<HaloCase, 3> HaloCases{
				HaloCase{
					{ 15, 0, 0 },
					{ 1, 0, 0 },
					{ { 1, 0, 0 }, { 0, 0, 0 } },
				},
				HaloCase{
					{ 15, 15, 0 },
					{ 1, 1, 0 },
					{ { 1, 1, 0 }, { 0, 0, 0 } },
				},
				HaloCase{
					{ 15, 15, 15 },
					{ 1, 1, 1 },
					{ { 1, 1, 1 }, { 0, 0, 0 } },
				},
			};

			bool haloCasesMatch = true;
			for (const HaloCase& haloCase : HaloCases)
			{
				SampleCoord globalSample{};
				OwnedSampleAddress address{};
				haloCasesMatch &=
					CellCornerToGlobalSample(
						haloCase.m_Cell,
						haloCase.m_Corner,
						globalSample).Succeeded() &&
					ResolveSampleOwner(
						globalSample,
						16,
						address).Succeeded() &&
					address == haloCase.m_Expected;
			}
			context.Check(
				haloCasesMatch,
				"Positive halo samples resolve through global sample ownership");
		}

		void RunCoordinateBoundsTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			SampleCoord maximumSample{};
			context.Check(
				ChunkLocalToGlobalSample(
					{ 134217727, 0, 0 },
					{ 15, 0, 0 },
					16,
					maximumSample).Succeeded() &&
				maximumSample ==
					SampleCoord{
						std::numeric_limits<std::int32_t>::max(),
						0,
						0,
					},
				"ChunkLocalToGlobalSample accepts the maximum exact coordinate");

			CellCoord minimumCell{};
			context.Check(
				ChunkLocalToGlobalCell(
					{ -134217728, 0, 0 },
					{ 0, 0, 0 },
					16,
					minimumCell).Succeeded() &&
				minimumCell ==
					CellCoord{
						std::numeric_limits<std::int32_t>::min(),
						0,
						0,
					},
				"ChunkLocalToGlobalCell accepts the minimum exact coordinate");

			SampleCoord unchangedSample{ 1, 2, 3 };
			context.Check(
				ChunkLocalToGlobalSample(
					{ std::numeric_limits<std::int32_t>::max(), 0, 0 },
					{ 0, 0, 0 },
					32,
					unchangedSample).m_Error ==
					ValidationError::CoordinateOutOfRange &&
				unchangedSample == SampleCoord{ 1, 2, 3 },
				"ChunkLocalToGlobalSample rejects expansion beyond int32");

			SampleAabb sampleBounds{};
			const CellAabb logicalCellBounds{
				.m_Min = { -16, -8, -4 },
				.m_MaxExclusive = { 16, 8, 4 },
			};
			context.Check(
				LogicalCellBoundsToSampleBounds(
					logicalCellBounds,
					sampleBounds).Succeeded() &&
				sampleBounds == SampleAabb{
					.m_Min = { -16, -8, -4 },
					.m_MaxExclusive = { 17, 9, 5 },
				} &&
				sampleBounds.Contains({ -16, -8, -4 }) &&
				sampleBounds.Contains({ 16, 8, 4 }) &&
				!sampleBounds.Contains({ 17, 8, 4 }),
				"Logical cell bounds expand by one positive sample");

			ChunkAabb sampleOwnerBounds{};
			context.Check(
				SampleBoundsToOwnerChunkBounds(
					sampleBounds,
					16,
					sampleOwnerBounds).Succeeded() &&
					sampleOwnerBounds ==
						ChunkAabb{
							.m_Min = { -1, -1, -1 },
							.m_MaxExclusive = { 2, 1, 1 },
						} &&
					sampleOwnerBounds.Contains({ 1, 0, 0 }) &&
					!sampleOwnerBounds.Contains({ 2, 0, 0 }),
				"Sample bounds identify cell and positive guard owner chunks");

			context.Check(
				LogicalCellBoundsToSampleBounds(
					{
						.m_Min = { 0, 0, 0 },
						.m_MaxExclusive = { 0, 1, 1 },
					},
					sampleBounds).m_Error ==
					ValidationError::EmptyLogicalCellBounds,
				"Logical sample bounds conversion rejects an empty cell axis");
			context.Check(
				LogicalCellBoundsToSampleBounds(
					{
						.m_Min = { 0, 0, 0 },
						.m_MaxExclusive = {
							std::numeric_limits<std::int32_t>::max(),
							1,
							1,
						},
					},
					sampleBounds).m_Error ==
					ValidationError::LogicalSampleBoundsOverflow,
				"Logical sample bounds conversion rejects positive overflow");
		}

		void RunCellOwnerChunkIntersectionTests(
			SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const CellAabb positiveBounds{
				.m_Min = { 4, 2, 1 },
				.m_MaxExclusive = { 20, 14, 9 },
			};
			CellAabb intersection{};
			const bool positivePartialChunksMatch =
				IntersectCellOwnerChunk(
					{ 0, 0, 0 },
					8,
					positiveBounds,
					intersection).Succeeded() &&
				intersection ==
					CellAabb{
						.m_Min = { 4, 2, 1 },
						.m_MaxExclusive = { 8, 8, 8 },
					} &&
				IntersectCellOwnerChunk(
					{ 2, 1, 1 },
					8,
					positiveBounds,
					intersection).Succeeded() &&
				intersection ==
					CellAabb{
						.m_Min = { 16, 8, 8 },
						.m_MaxExclusive = { 20, 14, 9 },
					};
			context.Check(
				positivePartialChunksMatch,
				"Cell-owner chunk intersection clips positive partial chunks");

			const CellAabb unchangedIntersection{
				.m_Min = { 1, 2, 3 },
				.m_MaxExclusive = { 4, 5, 6 },
			};
			intersection = unchangedIntersection;
			context.Check(
				IntersectCellOwnerChunk(
					{ -1, 0, 0 },
					8,
					positiveBounds,
					intersection).m_Error ==
						ValidationError::ChunkOutsideLogicalCellDomain &&
					intersection == unchangedIntersection,
				"Outside cell-owner chunks leave the intersection unchanged");

			const CellAabb negativeBounds{
				.m_Min = { -20, -14, -9 },
				.m_MaxExclusive = { -4, -2, -1 },
			};
			const bool negativePartialChunksMatch =
				IntersectCellOwnerChunk(
					{ -3, -2, -2 },
					8,
					negativeBounds,
					intersection).Succeeded() &&
				intersection ==
					CellAabb{
						.m_Min = { -20, -14, -9 },
						.m_MaxExclusive = { -16, -8, -8 },
					} &&
				IntersectCellOwnerChunk(
					{ -1, -1, -1 },
					8,
					negativeBounds,
					intersection).Succeeded() &&
				intersection ==
					CellAabb{
						.m_Min = { -8, -8, -8 },
						.m_MaxExclusive = { -4, -2, -1 },
					};
			context.Check(
				negativePartialChunksMatch,
				"Cell-owner chunk intersection preserves negative partial chunks");
		}

		void RunLogicalDomainMetricsTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeValidConfig();
			LogicalDomainMetrics metrics{};
			context.Check(
				ComputeLogicalDomainMetrics(config, metrics).Succeeded() &&
				metrics ==
					LogicalDomainMetrics{
						.m_CellCountX = 32,
						.m_CellCountY = 16,
						.m_CellCountZ = 8,
						.m_SampleCountX = 33,
						.m_SampleCountY = 17,
						.m_SampleCountZ = 9,
						.m_TotalCellCount = 4096,
						.m_TotalSampleCount = 5049,
						.m_CellOwnerChunkBounds = {
							.m_Min = { -1, -1, -1 },
							.m_MaxExclusive = { 1, 1, 1 },
						},
						.m_SampleOwnerChunkBounds = {
							.m_Min = { -1, -1, -1 },
							.m_MaxExclusive = { 2, 1, 1 },
						},
						.m_CellOwnerChunkCount = 8,
					},
				"Logical domain metrics describe cells, samples, and owner chunks");

			VoxelWorldConfig singleCellConfig = MakeValidConfig();
			singleCellConfig.m_LogicalCellBounds = {
				.m_Min = { -17, -16, -15 },
				.m_MaxExclusive = { -16, -15, -14 },
			};
			context.Check(
				ComputeLogicalDomainMetrics(
					singleCellConfig,
					metrics).Succeeded() &&
				metrics.m_TotalCellCount == 1 &&
				metrics.m_TotalSampleCount == 8 &&
				metrics.m_CellOwnerChunkBounds ==
					ChunkAabb{
						.m_Min = { -2, -1, -1 },
						.m_MaxExclusive = { -1, 0, 0 },
					} &&
				metrics.m_SampleOwnerChunkBounds ==
					ChunkAabb{
						.m_Min = { -2, -1, -1 },
						.m_MaxExclusive = { 0, 0, 0 },
					} &&
				metrics.m_CellOwnerChunkCount == 1,
				"Logical domain metrics preserve negative owner boundaries");

			const LogicalDomainMetrics unchangedMetrics{
				.m_CellCountX = 7,
				.m_CellCountY = 8,
				.m_CellCountZ = 9,
				.m_CellOwnerChunkCount = 10,
			};
			metrics = unchangedMetrics;
			VoxelWorldConfig invalidConfig = MakeValidConfig();
			invalidConfig.m_ChunkCellCount = 7;
			context.Check(
				ComputeLogicalDomainMetrics(
					invalidConfig,
					metrics).m_Error ==
					ValidationError::InvalidChunkCellCount &&
				metrics == unchangedMetrics,
				"Failed logical domain computation leaves metrics unchanged");

			VoxelWorldConfig cellOverflowConfig = MakeValidConfig();
			cellOverflowConfig.m_LogicalCellBounds = {
				.m_Min = {
					std::numeric_limits<std::int32_t>::min(),
					std::numeric_limits<std::int32_t>::min(),
					std::numeric_limits<std::int32_t>::min(),
				},
				.m_MaxExclusive = {
					std::numeric_limits<std::int32_t>::max() - 1,
					std::numeric_limits<std::int32_t>::max() - 1,
					std::numeric_limits<std::int32_t>::max() - 1,
				},
			};
			context.Check(
				ComputeLogicalDomainMetrics(
					cellOverflowConfig,
					metrics).m_Error ==
					ValidationError::LogicalCellCountOverflow &&
				ValidateConfig(cellOverflowConfig).m_Error ==
					ValidationError::LogicalCellCountOverflow,
				"Logical domain validation rejects uint64 cell count overflow");

			VoxelWorldConfig sampleOverflowConfig = MakeValidConfig();
			sampleOverflowConfig.m_LogicalCellBounds = {
				.m_Min = {
					std::numeric_limits<std::int32_t>::min(),
					std::numeric_limits<std::int32_t>::min(),
					0,
				},
				.m_MaxExclusive = {
					std::numeric_limits<std::int32_t>::max() - 1,
					std::numeric_limits<std::int32_t>::max() - 1,
					1,
				},
			};
			context.Check(
				ComputeLogicalDomainMetrics(
					sampleOverflowConfig,
					metrics).m_Error ==
					ValidationError::LogicalSampleCountOverflow &&
				ValidateConfig(sampleOverflowConfig).m_Error ==
					ValidationError::LogicalSampleCountOverflow,
				"Logical domain validation rejects uint64 sample count overflow");
		}

		void RunWorldConfigValidationTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			VoxelWorldConfig config = MakeValidConfig();
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig accepts chunk cell count 16");

			config.m_ChunkCellCount = 8;
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig accepts chunk cell count 8");

			config.m_ChunkCellCount = 32;
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig accepts chunk cell count 32");

			config = MakeValidConfig();
			config.m_ChunkCellCount = 7;
			CheckValidationError(
				context,
				config,
				ValidationError::InvalidChunkCellCount,
				"ValidateConfig rejects an unsupported chunk cell count");

			config = MakeValidConfig();
			config.m_VoxelSize = 0.0f;
			CheckValidationError(
				context,
				config,
				ValidationError::NonPositiveVoxelSize,
				"ValidateConfig rejects zero voxel size");

			config.m_VoxelSize = -1.0f;
			CheckValidationError(
				context,
				config,
				ValidationError::NonPositiveVoxelSize,
				"ValidateConfig rejects negative voxel size");

			config.m_VoxelSize = std::numeric_limits<float>::infinity();
			CheckValidationError(
				context,
				config,
				ValidationError::NonFiniteVoxelSize,
				"ValidateConfig rejects infinite voxel size");

			config.m_VoxelSize = std::numeric_limits<float>::quiet_NaN();
			CheckValidationError(
				context,
				config,
				ValidationError::NonFiniteVoxelSize,
				"ValidateConfig rejects NaN voxel size");

			config = MakeValidConfig();
			config.m_SurfaceBandVoxels = 0.0f;
			CheckValidationError(
				context,
				config,
				ValidationError::NonPositiveSurfaceBandVoxels,
				"ValidateConfig rejects zero surface band");

			config.m_SurfaceBandVoxels = -1.0f;
			CheckValidationError(
				context,
				config,
				ValidationError::NonPositiveSurfaceBandVoxels,
				"ValidateConfig rejects negative surface band");

			config.m_SurfaceBandVoxels =
				-std::numeric_limits<float>::infinity();
			CheckValidationError(
				context,
				config,
				ValidationError::NonFiniteSurfaceBandVoxels,
				"ValidateConfig rejects infinite surface band");

			config.m_SurfaceBandVoxels =
				std::numeric_limits<float>::quiet_NaN();
			CheckValidationError(
				context,
				config,
				ValidationError::NonFiniteSurfaceBandVoxels,
				"ValidateConfig rejects NaN surface band");

			config = MakeValidConfig();
			config.m_LogicalCellBounds.m_MaxExclusive.m_X =
				config.m_LogicalCellBounds.m_Min.m_X;
			CheckValidationError(
				context,
				config,
				ValidationError::EmptyLogicalCellBounds,
				"ValidateConfig rejects an empty logical axis");

			config = MakeValidConfig();
			config.m_LogicalCellBounds.m_MaxExclusive.m_Z =
				config.m_LogicalCellBounds.m_Min.m_Z - 1;
			CheckValidationError(
				context,
				config,
				ValidationError::EmptyLogicalCellBounds,
				"ValidateConfig rejects an inverted logical axis");

			config = MakeValidConfig();
			config.m_LogicalCellBounds.m_MaxExclusive.m_Y =
				std::numeric_limits<std::int32_t>::max();
			CheckValidationError(
				context,
				config,
				ValidationError::LogicalSampleBoundsOverflow,
				"ValidateConfig rejects overflowing logical sample bounds");

			config = MakeValidConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { 1000000, 0, 0 },
				.m_MaxExclusive = { 1000008, 8, 8 },
			};
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig accepts distant chunk-local mesh domains");

			config = MakeValidConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { 16777215, 0, 0 },
				.m_MaxExclusive = { 16777218, 8, 8 },
			};
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig does not convert logical coordinates to Float3");

			config = MakeValidConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { -128, -128, -128 },
				.m_MaxExclusive = { 127, 127, 127 },
			};
			context.Check(
				ValidateConfig(config).Succeeded(),
				"ValidateConfig accepts negative multi-chunk domains");

			config = MakeValidConfig();
			config.m_VoxelSize =
				std::numeric_limits<float>::max();
			CheckValidationError(
				context,
				config,
				ValidationError::
					UnrepresentableChunkLocalPosition,
				"ValidateConfig rejects chunk-local positions that overflow Float3");

			config = MakeValidConfig();
			config.m_VoxelSize =
				std::numeric_limits<float>::denorm_min();
			CheckValidationError(
				context,
				config,
				ValidationError::
					UnrepresentableChunkLocalPosition,
				"ValidateConfig rejects voxel sizes below canonical local precision");
		}
	}

	void RunNapaVoxelCoordinateSelfTests(SelfTestContext& context) noexcept
	{
		RunDouble3MathTests(context);
		RunCheckedArithmeticTests(context);
		RunFloorDivisionTests(context);
		RunCoordinateOwnershipTests(context);
		RunCanonicalOrderingTests(context);
		RunFlattenTests(context);
		RunCellCornerAndHaloTests(context);
		RunCoordinateBoundsTests(context);
		RunCellOwnerChunkIntersectionTests(context);
		RunLogicalDomainMetricsTests(context);
		RunWorldConfigValidationTests(context);
	}
}
