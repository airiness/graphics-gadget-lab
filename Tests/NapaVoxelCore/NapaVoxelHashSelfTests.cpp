#include "NapaVoxelTestFramework.h"

#include "NapaVoxelCore/Hash/CanonicalHash.h"
#include "NapaVoxelCore/Hash/VoxelWorldHash.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <cstdint>
#include <memory>

namespace napa::voxel::testing
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeGoldenHashConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -1, -1, -1 },
					.m_MaxExclusive = { 1, 1, 1 },
				},
			};
		}

		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeAllocationHashConfig() noexcept
		{
			napa::voxel::VoxelWorldConfig config = MakeGoldenHashConfig();
			config.m_LogicalCellBounds = {
				.m_Min = { -8, -8, -8 },
				.m_MaxExclusive = { 8, 8, 8 },
			};
			return config;
		}

		void RunCanonicalHashWriterTests(
			TestContext& context) noexcept
		{
			using namespace napa::voxel;

			CanonicalHashWriter empty;
			context.Check(
				empty.GetValue() == Fnv1a64OffsetBasis,
				"An empty FNV-1a stream returns the offset basis");

			CanonicalHashWriter hello;
			for (const std::uint8_t byte : {
				std::uint8_t{ 'h' },
				std::uint8_t{ 'e' },
				std::uint8_t{ 'l' },
				std::uint8_t{ 'l' },
				std::uint8_t{ 'o' },
			})
			{
				hello.WriteU8(byte);
			}
			context.Check(
				hello.GetValue() == 0xa430d84680aabd0bull,
				"Canonical hashing matches the FNV-1a hello vector");

			CanonicalHashWriter u8;
			u8.WriteU8(0x12);
			context.Check(
				u8.GetValue() == 0xaf63cf4c8601d675ull,
				"WriteU8 matches its known byte vector");

			CanonicalHashWriter u16;
			u16.WriteU16(0x1234);
			context.Check(
				u16.GetValue() == 0x07ee9e07b4b1c883ull,
				"WriteU16 serializes little-endian bytes");

			CanonicalHashWriter i16;
			CanonicalHashWriter i16Bits;
			i16.WriteI16(-32767);
			i16Bits.WriteU16(0x8001);
			context.Check(
				i16.GetValue() == i16Bits.GetValue(),
				"WriteI16 serializes two's-complement little-endian bits");

			CanonicalHashWriter u32;
			u32.WriteU32(0x12345678);
			context.Check(
				u32.GetValue() == 0xcccfd053e47c3365ull,
				"WriteU32 serializes little-endian bytes");

			CanonicalHashWriter u64;
			u64.WriteU64(0x0123456789abcdefull);
			context.Check(
				u64.GetValue() == 0x37eb3f3347761c55ull,
				"WriteU64 serializes little-endian bytes");

			CanonicalHashWriter i32;
			i32.WriteI32(-2);
			context.Check(
				i32.GetValue() == 0x7053767088d9a6c0ull,
				"WriteI32 serializes two's-complement bits");

			CanonicalHashWriter i64;
			i64.WriteI64(-2);
			context.Check(
				i64.GetValue() == 0xfc1a35225397861cull,
				"WriteI64 serializes two's-complement bits");

			CanonicalHashWriter positiveZero32;
			CanonicalHashWriter negativeZero32;
			positiveZero32.WriteFloat32(0.0f);
			negativeZero32.WriteFloat32(-0.0f);
			context.Check(
				positiveZero32.GetValue() == 0x4d25767f9dce13f5ull &&
					negativeZero32.GetValue() ==
						positiveZero32.GetValue(),
				"WriteFloat32 canonicalizes negative zero");

			CanonicalHashWriter positiveZero64;
			CanonicalHashWriter negativeZero64;
			positiveZero64.WriteFloat64(0.0);
			negativeZero64.WriteFloat64(-0.0);
			context.Check(
				positiveZero64.GetValue() == 0xa8c7f832281a39c5ull &&
					negativeZero64.GetValue() ==
						positiveZero64.GetValue(),
				"WriteFloat64 canonicalizes negative zero");

			CanonicalHashWriter enumWriter;
			CanonicalHashWriter enumUnderlyingWriter;
			enumWriter.WriteEnum(VoxelMaterial::Stone);
			enumUnderlyingWriter.WriteU8(
				static_cast<std::uint8_t>(VoxelMaterial::Stone));
			context.Check(
				enumWriter.GetValue() ==
					enumUnderlyingWriter.GetValue(),
				"WriteEnum uses the declared underlying integer width");

			CanonicalHashWriter count32;
			CanonicalHashWriter count32Underlying;
			count32.WriteCount(std::uint32_t{ 0x12345678 });
			count32Underlying.WriteU32(0x12345678);
			CanonicalHashWriter count64;
			CanonicalHashWriter count64Underlying;
			count64.WriteCount(std::uint64_t{ 0x0123456789abcdefull });
			count64Underlying.WriteU64(0x0123456789abcdefull);
			context.Check(
				count32.GetValue() ==
					count32Underlying.GetValue() &&
					count64.GetValue() ==
						count64Underlying.GetValue(),
				"WriteCount preserves the selected canonical width");
		}

		void RunLogicalVoxelWorldHashTests(
			TestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::unique_ptr<VoxelWorld> goldenWorld;
			std::uint64_t goldenHash = 0;
			context.Check(
				VoxelWorld::Create(
					MakeGoldenHashConfig(),
					goldenWorld).Succeeded() &&
					goldenWorld &&
					ComputeLogicalVoxelWorldHash(
						*goldenWorld,
						goldenHash).Succeeded() &&
					goldenHash == 0x3447718e07eed74aull,
				"The default logical voxel world matches its golden hash");

			std::unique_ptr<VoxelWorld> nonEmptyGoldenWorld;
			std::uint64_t nonEmptyGoldenHash = 0;
			const VoxelSample goldenStone{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 17,
			};
			bool goldenChanged = false;
			context.Check(
				VoxelWorld::Create(
					MakeGoldenHashConfig(),
					nonEmptyGoldenWorld).Succeeded() &&
					nonEmptyGoldenWorld &&
					nonEmptyGoldenWorld->WriteOriginalAndCurrentSample(
						{ 0, 0, 0 },
						goldenStone,
						goldenChanged).Succeeded() &&
					goldenChanged &&
					ComputeLogicalVoxelWorldHash(
						*nonEmptyGoldenWorld,
						nonEmptyGoldenHash).Succeeded() &&
					nonEmptyGoldenHash == 0x22acf3d1636e3855ull,
				"A non-empty logical voxel world matches its golden hash");

			const VoxelWorldConfig config = MakeAllocationHashConfig();
			std::unique_ptr<VoxelWorld> implicitWorld;
			std::unique_ptr<VoxelWorld> allocatedWorld;
			bool allocated = false;
			std::uint64_t implicitHash = 0;
			std::uint64_t allocatedHash = 0;
			context.Check(
				VoxelWorld::Create(config, implicitWorld).Succeeded() &&
					VoxelWorld::Create(config, allocatedWorld).Succeeded() &&
					implicitWorld &&
					allocatedWorld &&
					allocatedWorld->EnsureChunkAllocated(
						{ 1, 0, 0 },
						allocated).Succeeded() &&
					allocated &&
					allocatedWorld->EnsureChunkAllocated(
						{ 0, 0, 0 },
						allocated).Succeeded() &&
					allocated &&
					ComputeLogicalVoxelWorldHash(
						*implicitWorld,
						implicitHash).Succeeded() &&
					ComputeLogicalVoxelWorldHash(
						*allocatedWorld,
						allocatedHash).Succeeded() &&
					implicitHash == allocatedHash,
				"Logical voxel hashing ignores resident and guard allocation");
			const std::uint64_t defaultAllocationHash = implicitHash;

			const VoxelSample stone{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 17,
			};
			const VoxelSample soil{
				.m_Density = IsoValue + 1,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 3,
			};
			const SampleCoord firstCoordinate{ -8, -1, 7 };
			const SampleCoord secondCoordinate{ 8, 8, 8 };
			bool changed = false;
			context.Check(
				implicitWorld->WriteCurrentSample(
					firstCoordinate,
					stone,
					changed).Succeeded() &&
					changed &&
					implicitWorld->WriteCurrentSample(
						secondCoordinate,
						soil,
						changed).Succeeded() &&
					changed &&
					allocatedWorld->WriteCurrentSample(
						secondCoordinate,
						soil,
						changed).Succeeded() &&
					changed &&
					allocatedWorld->WriteCurrentSample(
						firstCoordinate,
						stone,
						changed).Succeeded() &&
					changed &&
					ComputeLogicalVoxelWorldHash(
						*implicitWorld,
						implicitHash).Succeeded() &&
					ComputeLogicalVoxelWorldHash(
						*allocatedWorld,
						allocatedHash).Succeeded() &&
					implicitHash == allocatedHash,
				"Logical voxel hashing is independent of allocation and write order");

			std::unique_ptr<VoxelWorld> clearedWorld;
			std::uint64_t clearedHash = 0;
			context.Check(
				VoxelWorld::Create(config, clearedWorld).Succeeded() &&
					clearedWorld &&
					clearedWorld->WriteCurrentSample(
						firstCoordinate,
						stone,
						changed).Succeeded() &&
					changed &&
					clearedWorld->WriteCurrentSample(
						firstCoordinate,
						DefaultVoxelSample,
						changed).Succeeded() &&
					changed &&
					clearedWorld->GetResidentChunkCount() == 1 &&
					ComputeLogicalVoxelWorldHash(
						*clearedWorld,
						clearedHash).Succeeded() &&
					clearedHash == defaultAllocationHash,
				"Clearing current data restores the allocation-independent hash");
		}
	}

	void RunNapaVoxelHashSelfTests(TestContext& context) noexcept
	{
		RunCanonicalHashWriterTests(context);
		RunLogicalVoxelWorldHashTests(context);
	}
}
