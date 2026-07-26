#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/Validation/CheckedArithmetic.h"
#include "NapaVoxelCore/World/VoxelWorldConfig.h"

#include <array>
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

			context.Check(
				P0IsoValue == 128,
				"The P0 iso value remains fixed at 128");
		}
	}

	void RunNapaVoxelCoordinateSelfTests(SelfTestContext& context) noexcept
	{
		RunCheckedArithmeticTests(context);
		RunWorldConfigValidationTests(context);
	}
}
