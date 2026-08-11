#include "Core/Precompiled.h"
#include "Application/SelfTest/NapaVoxelCoreSelfTestCases.h"

#include "NapaVoxelCore/World/VoxelChunk.h"
#include "NapaVoxelCore/World/VoxelSample.h"
#include "NapaVoxelCore/World/VoxelWorld.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>

namespace gglab
{
	namespace
	{
		[[nodiscard]] napa::voxel::VoxelWorldConfig
			MakeStorageConfig() noexcept
		{
			return {
				.m_ChunkCellCount = 8,
				.m_VoxelSize = 0.25f,
				.m_SurfaceBandVoxels = 2.0f,
				.m_LogicalCellBounds = {
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 8, 8, 8 },
				},
			};
		}

		void RunVoxelSampleTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			context.Check(IsoValue == 128, "The canonical iso value is 128");
			context.Check(
				std::is_same_v<
				std::underlying_type_t<VoxelMaterial>,
				std::uint8_t>,
				"VoxelMaterial uses uint8_t as its underlying type");
			context.Check(
				std::is_standard_layout_v<VoxelSample>,
				"VoxelSample has standard layout");
			context.Check(
				std::is_trivially_copyable_v<VoxelSample>,
				"VoxelSample is trivially copyable");
			context.Check(
				sizeof(VoxelSample) == 3,
				"VoxelSample occupies exactly three bytes");
			context.Check(
				offsetof(VoxelSample, m_Density) == 0 &&
				offsetof(VoxelSample, m_Material) == 1 &&
				offsetof(VoxelSample, m_Damage) == 2,
				"VoxelSample fields have the canonical byte offsets");

			context.Check(
				DefaultVoxelSample ==
				VoxelSample{
					.m_Density = 0,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				} &&
				ValidateVoxelSample(DefaultVoxelSample).Succeeded(),
				"The default voxel sample is canonical empty");

			const VoxelSample nonCanonicalEmpty{
				.m_Density = IsoValue - 1,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 37,
			};
			VoxelSample prepared{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 11,
			};
			context.Check(
				ValidateVoxelSample(nonCanonicalEmpty).m_Error ==
				ValidationError::NonCanonicalVoxelSample,
				"Voxel validation detects non-canonical empty data");
			context.Check(
				PrepareVoxelSampleForStorage(
					nonCanonicalEmpty,
					prepared).Succeeded() &&
				prepared ==
				VoxelSample{
					.m_Density = IsoValue - 1,
					.m_Material = VoxelMaterial::Empty,
					.m_Damage = 0,
				} &&
				ValidateVoxelSample(prepared).Succeeded(),
				"Storage preparation canonicalizes a known empty sample");

			const VoxelSample solid{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 41,
			};
			context.Check(
				PrepareVoxelSampleForStorage(solid, prepared).Succeeded() &&
				prepared == solid &&
				ValidateVoxelSample(prepared).Succeeded(),
				"Storage preparation preserves a valid solid sample");

			const VoxelSample unchanged = prepared;
			const VoxelSample solidWithoutMaterial{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Empty,
				.m_Damage = 0,
			};
			context.Check(
				PrepareVoxelSampleForStorage(
					solidWithoutMaterial,
					prepared).m_Error ==
				ValidationError::NonCanonicalVoxelSample &&
				prepared == unchanged,
				"Storage preparation rejects solid data without a material");

			const VoxelSample unknownEmpty{
				.m_Density = 0,
				.m_Material = static_cast<VoxelMaterial>(255),
				.m_Damage = 20,
			};
			context.Check(
				PrepareVoxelSampleForStorage(
					unknownEmpty,
					prepared).m_Error ==
				ValidationError::InvalidVoxelMaterial &&
				prepared == unchanged,
				"Storage preparation rejects unknown material before normalization");
			context.Check(
				ValidateVoxelSample(unknownEmpty).m_Error ==
				ValidationError::InvalidVoxelMaterial,
				"Voxel validation rejects unknown materials independently");
		}

		void RunVoxelChunkTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			std::unique_ptr<VoxelChunk> chunk;
			context.Check(
				VoxelChunk::Create(8, chunk).Succeeded() &&
				chunk &&
				chunk->GetChunkCellCount() == 8 &&
				chunk->GetSampleCount() == 512 &&
				chunk->GetVoxelRevision() == 0,
				"A voxel chunk starts with the expected capacity and revision");
			if (!chunk)
			{
				return;
			}

			VoxelChunk* const validChunk = chunk.get();
			context.Check(
				VoxelChunk::Create(7, chunk).m_Error ==
				ValidationError::InvalidChunkCellCount &&
				chunk.get() == validChunk,
				"VoxelChunk creation rejects an unsupported permanent state");

			const LocalCoord local{ 7, 3, 1 };
			VoxelSample original{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 1,
			};
			VoxelSample current = original;
			context.Check(
				chunk->ReadOriginalSample(local, original).Succeeded() &&
				chunk->ReadCurrentSample(local, current).Succeeded() &&
				original == DefaultVoxelSample &&
				current == DefaultVoxelSample,
				"A voxel chunk initializes original and current samples to empty");

			const VoxelSample solid{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 41,
			};
			bool changed = false;
			context.Check(
				chunk->WriteOriginalAndCurrentSample(
					local,
					solid,
					changed).Succeeded() &&
				changed &&
				chunk->GetVoxelRevision() == 1 &&
				chunk->ReadOriginalSample(local, original).Succeeded() &&
				chunk->ReadCurrentSample(local, current).Succeeded() &&
				original == solid &&
				current == solid,
				"An initialization write updates both sample layers once");

			changed = true;
			context.Check(
				chunk->WriteOriginalAndCurrentSample(
					local,
					solid,
					changed).Succeeded() &&
				!changed &&
				chunk->GetVoxelRevision() == 1,
				"An identical initialization write does not advance revision");

			VoxelSample damaged = solid;
			damaged.m_Damage = 99;
			context.Check(
				chunk->WriteCurrentSample(
					local,
					damaged,
					changed).Succeeded() &&
				changed &&
				chunk->GetVoxelRevision() == 2 &&
				chunk->ReadOriginalSample(local, original).Succeeded() &&
				chunk->ReadCurrentSample(local, current).Succeeded() &&
				original == solid &&
				current == damaged,
				"A current write does not alias the original sample layer");

			const VoxelSample unknown{
				.m_Density = 0,
				.m_Material = static_cast<VoxelMaterial>(255),
				.m_Damage = 20,
			};
			changed = true;
			context.Check(
				chunk->WriteCurrentSample(
					local,
					unknown,
					changed).m_Error ==
				ValidationError::InvalidVoxelMaterial &&
				changed &&
				chunk->GetVoxelRevision() == 2 &&
				chunk->ReadCurrentSample(local, current).Succeeded() &&
				current == damaged,
				"A rejected chunk write changes neither data nor outputs");

			VoxelSample unchangedRead = damaged;
			context.Check(
				chunk->ReadCurrentSample(
					{ 8, 0, 0 },
					unchangedRead).m_Error ==
				ValidationError::InvalidLocalCoordinate &&
				unchangedRead == damaged,
				"Chunk reads reject local coordinates outside storage");
		}

		void RunVoxelWorldTests(SelfTestContext& context) noexcept
		{
			using namespace napa::voxel;

			const VoxelWorldConfig config = MakeStorageConfig();
			std::unique_ptr<VoxelWorld> world;
			context.Check(
				VoxelWorld::Create(config, world).Succeeded() &&
				world &&
				world->GetConfig().m_ChunkCellCount == 8 &&
				world->GetLogicalSampleBounds() ==
				SampleAabb{
					.m_Min = { -8, -8, -8 },
					.m_MaxExclusive = { 9, 9, 9 },
				} &&
				world->GetLogicalDomainMetrics().m_TotalSampleCount ==
				4913 &&
				world->GetLogicalDomainMetrics().m_CellOwnerChunkBounds ==
				ChunkAabb{
					.m_Min = { -1, -1, -1 },
					.m_MaxExclusive = { 1, 1, 1 },
				} &&
				world->GetLogicalDomainMetrics().m_SampleOwnerChunkBounds ==
				ChunkAabb{
					.m_Min = { -1, -1, -1 },
					.m_MaxExclusive = { 2, 2, 2 },
				} &&
				world->GetResidentChunkCount() == 0 &&
				world->GetWorldVoxelRevision() == 0,
				"A voxel world captures validated logical-domain metadata");
			if (!world)
			{
				return;
			}

			VoxelSample read{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 1,
			};
			context.Check(
				world->ReadCurrentSample({ -1, -1, -1 }, read).Succeeded() &&
				read == DefaultVoxelSample &&
				world->GetResidentChunkCount() == 0,
				"An unallocated logical sample reads as canonical empty");

			bool changed = true;
			context.Check(
				world->WriteCurrentSample(
					{ 0, 0, 0 },
					DefaultVoxelSample,
					changed).Succeeded() &&
				!changed &&
				world->GetResidentChunkCount() == 0 &&
				world->GetWorldVoxelRevision() == 0,
				"Writing default data does not allocate an empty chunk");

			const VoxelSample unknown{
				.m_Density = 0,
				.m_Material = static_cast<VoxelMaterial>(255),
				.m_Damage = 20,
			};
			changed = true;
			context.Check(
				world->WriteCurrentSample(
					{ 0, 0, 0 },
					unknown,
					changed).m_Error ==
				ValidationError::InvalidVoxelMaterial &&
				changed &&
				world->GetResidentChunkCount() == 0 &&
				world->GetWorldVoxelRevision() == 0,
				"An invalid sample cannot allocate or mutate world storage");

			const VoxelSample solidWithoutMaterial{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Empty,
				.m_Damage = 0,
			};
			context.Check(
				world->WriteCurrentSample(
					{ 0, 0, 0 },
					solidWithoutMaterial,
					changed).m_Error ==
				ValidationError::NonCanonicalVoxelSample &&
				world->GetResidentChunkCount() == 0,
				"A solid sample without material cannot enter world storage");

			const SampleCoord negativeCoordinate{ -1, -1, -1 };
			const VoxelSample solid{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Stone,
				.m_Damage = 41,
			};
			changed = false;
			context.Check(
				world->WriteOriginalAndCurrentSample(
					negativeCoordinate,
					solid,
					changed).Succeeded() &&
				changed &&
				world->GetResidentChunkCount() == 1 &&
				world->GetWorldVoxelRevision() == 1,
				"A negative global write allocates its canonical owner chunk");

			const VoxelChunk* negativeChunk =
				world->FindChunk({ -1, -1, -1 });
			VoxelSample original{};
			VoxelSample current{};
			context.Check(
				negativeChunk &&
				negativeChunk->GetVoxelRevision() == 1 &&
				world->ReadOriginalSample(
					negativeCoordinate,
					original).Succeeded() &&
				world->ReadCurrentSample(
					negativeCoordinate,
					current).Succeeded() &&
				original == solid &&
				current == solid,
				"World reads recover original and current data across negatives");

			changed = true;
			context.Check(
				world->WriteOriginalAndCurrentSample(
					negativeCoordinate,
					solid,
					changed).Succeeded() &&
				!changed &&
				world->GetWorldVoxelRevision() == 1 &&
				negativeChunk->GetVoxelRevision() == 1,
				"An identical world write advances no revisions");

			VoxelSample damaged = solid;
			damaged.m_Damage = 99;
			context.Check(
				world->WriteCurrentSample(
					negativeCoordinate,
					damaged,
					changed).Succeeded() &&
				changed &&
				world->GetWorldVoxelRevision() == 2 &&
				negativeChunk->GetVoxelRevision() == 2 &&
				world->ReadOriginalSample(
					negativeCoordinate,
					original).Succeeded() &&
				world->ReadCurrentSample(
					negativeCoordinate,
					current).Succeeded() &&
				original == solid &&
				current == damaged,
				"World current writes preserve independent original data");

			const VoxelSample positiveHalo{
				.m_Density = IsoValue,
				.m_Material = VoxelMaterial::Soil,
				.m_Damage = 7,
			};
			context.Check(
				world->WriteOriginalAndCurrentSample(
					{ 8, 0, 0 },
					positiveHalo,
					changed).Succeeded() &&
				changed &&
				world->FindChunk({ 1, 0, 0 }) &&
				world->GetResidentChunkCount() == 2 &&
				world->GetWorldVoxelRevision() == 3,
				"The positive logical halo sample has authoritative storage");

			read = damaged;
			context.Check(
				world->ReadCurrentSample({ 9, 0, 0 }, read).m_Error ==
				ValidationError::SampleOutsideLogicalBounds &&
				read == damaged,
				"Logical bounds reject padding inside an allocated halo chunk");

			bool allocated = false;
			context.Check(
				world->EnsureChunkAllocated({ 0, 0, 1 }, allocated).Succeeded() &&
				allocated &&
				world->GetResidentChunkCount() == 3 &&
				world->GetWorldVoxelRevision() == 3,
				"A positive guard sample chunk can be allocated without changing revision");
			allocated = true;
			context.Check(
				world->EnsureChunkAllocated({ 0, 0, 1 }, allocated).Succeeded() &&
				!allocated &&
				world->GetResidentChunkCount() == 3,
				"Guard allocation reports an already resident chunk");
			allocated = true;
			context.Check(
				world->EnsureChunkAllocated({ 4, 0, 0 }, allocated).m_Error ==
				ValidationError::ChunkOutsideLogicalSampleDomain &&
				allocated &&
				world->GetResidentChunkCount() == 3,
				"Chunk allocation rejects owners unrelated to the logical sample domain");

			const std::size_t residentBeforeRejectedWrite =
				world->GetResidentChunkCount();
			const std::uint64_t revisionBeforeRejectedWrite =
				world->GetWorldVoxelRevision();
			changed = true;
			context.Check(
				world->WriteCurrentSample(
					{ 0, 0, 9 },
					unknown,
					changed).m_Error ==
				ValidationError::SampleOutsideLogicalBounds &&
				changed &&
				world->GetResidentChunkCount() ==
				residentBeforeRejectedWrite &&
				world->GetWorldVoxelRevision() ==
				revisionBeforeRejectedWrite,
				"World writes validate logical bounds before sample data");

			read = damaged;
			context.Check(
				world->ReadCurrentSample({ 0, 0, 9 }, read).m_Error ==
				ValidationError::SampleOutsideLogicalBounds &&
				read == damaged,
				"Allocated guard chunks remain inaccessible as logical samples");

			std::unique_ptr<VoxelWorld> currentFirstWorld;
			bool currentFirstChanged = false;
			VoxelSample currentFirstOriginal{};
			VoxelSample currentFirstCurrent{};
			context.Check(
				VoxelWorld::Create(config, currentFirstWorld).Succeeded() &&
				currentFirstWorld &&
				currentFirstWorld->WriteCurrentSample(
					negativeCoordinate,
					solid,
					currentFirstChanged).Succeeded() &&
				currentFirstChanged &&
				currentFirstWorld->GetResidentChunkCount() == 1 &&
				currentFirstWorld->GetWorldVoxelRevision() == 1 &&
				currentFirstWorld->ReadOriginalSample(
					negativeCoordinate,
					currentFirstOriginal).Succeeded() &&
				currentFirstWorld->ReadCurrentSample(
					negativeCoordinate,
					currentFirstCurrent).Succeeded() &&
				currentFirstOriginal == DefaultVoxelSample &&
				currentFirstCurrent == solid,
				"A current-first write allocates storage without changing original data");

			VoxelWorld* const existingWorld = world.get();
			VoxelWorldConfig invalidConfig = config;
			invalidConfig.m_ChunkCellCount = 7;
			context.Check(
				VoxelWorld::Create(invalidConfig, world).m_Error ==
				ValidationError::InvalidChunkCellCount &&
				world.get() == existingWorld,
				"Failed world creation leaves the output world unchanged");
		}
	}

	void RunNapaVoxelStorageSelfTests(SelfTestContext& context) noexcept
	{
		RunVoxelSampleTests(context);
		RunVoxelChunkTests(context);
		RunVoxelWorldTests(context);
	}
}
