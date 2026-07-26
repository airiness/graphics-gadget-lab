#include "NapaVoxelCore/World/VoxelWorld.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace napa::voxel
{
	namespace
	{
		[[nodiscard]] SampleAabb IntersectSampleOwnerChunk(
			ChunkCoord chunk,
			std::uint32_t chunkCellCount,
			const SampleAabb& logicalBounds) noexcept
		{
			const std::int64_t chunkSize = chunkCellCount;
			const std::int64_t minimumX =
				static_cast<std::int64_t>(chunk.m_X) * chunkSize;
			const std::int64_t minimumY =
				static_cast<std::int64_t>(chunk.m_Y) * chunkSize;
			const std::int64_t minimumZ =
				static_cast<std::int64_t>(chunk.m_Z) * chunkSize;
			const std::int64_t maximumX = minimumX + chunkSize;
			const std::int64_t maximumY = minimumY + chunkSize;
			const std::int64_t maximumZ = minimumZ + chunkSize;

			return {
				.m_Min = {
					static_cast<std::int32_t>(std::max(
						minimumX,
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_X))),
					static_cast<std::int32_t>(std::max(
						minimumY,
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_Y))),
					static_cast<std::int32_t>(std::max(
						minimumZ,
						static_cast<std::int64_t>(
							logicalBounds.m_Min.m_Z))),
				},
				.m_MaxExclusive = {
					static_cast<std::int32_t>(std::min(
						maximumX,
						static_cast<std::int64_t>(
							logicalBounds.m_MaxExclusive.m_X))),
					static_cast<std::int32_t>(std::min(
						maximumY,
						static_cast<std::int64_t>(
							logicalBounds.m_MaxExclusive.m_Y))),
					static_cast<std::int32_t>(std::min(
						maximumZ,
						static_cast<std::int64_t>(
							logicalBounds.m_MaxExclusive.m_Z))),
				},
			};
		}
	}

	ValidationResult VoxelWorld::Create(
		const VoxelWorldConfig& config,
		std::unique_ptr<VoxelWorld>& world)
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

		SampleAabb sampleBounds{};
		const ValidationResult boundsResult =
			LogicalCellBoundsToSampleBounds(
				config.m_LogicalCellBounds,
				sampleBounds);
		if (boundsResult.Failed())
		{
			return boundsResult;
		}

		std::unique_ptr<VoxelWorld> created =
			std::make_unique<VoxelWorld>(
				ConstructionToken{},
				config,
				metrics,
				sampleBounds);
		world = std::move(created);
		return {};
	}

	VoxelWorld::VoxelWorld(
		ConstructionToken,
		const VoxelWorldConfig& config,
		const LogicalDomainMetrics& metrics,
		SampleAabb logicalSampleBounds)
		: m_Config(config)
		, m_LogicalDomainMetrics(metrics)
		, m_LogicalSampleBounds(logicalSampleBounds)
	{
	}

	const VoxelWorldConfig& VoxelWorld::GetConfig() const noexcept
	{
		return m_Config;
	}

	const LogicalDomainMetrics& VoxelWorld::GetLogicalDomainMetrics()
		const noexcept
	{
		return m_LogicalDomainMetrics;
	}

	SampleAabb VoxelWorld::GetLogicalSampleBounds() const noexcept
	{
		return m_LogicalSampleBounds;
	}

	std::size_t VoxelWorld::GetResidentChunkCount() const noexcept
	{
		return m_Chunks.size();
	}

	std::uint64_t VoxelWorld::GetWorldVoxelRevision() const noexcept
	{
		return m_WorldVoxelRevision;
	}

	const VoxelChunk* VoxelWorld::FindChunk(ChunkCoord chunk) const noexcept
	{
		const auto iterator = m_Chunks.find(chunk);
		return iterator != m_Chunks.end()
			? iterator->second.get()
			: nullptr;
	}

	ValidationResult VoxelWorld::EnsureChunkAllocated(
		ChunkCoord chunk,
		bool& allocated)
	{
		if (!m_LogicalDomainMetrics.m_SampleOwnerChunkBounds.Contains(chunk))
		{
			return {
				ValidationError::ChunkOutsideLogicalSampleDomain,
			};
		}

		VoxelChunk* result = nullptr;
		return FindOrCreateChunk(chunk, result, allocated);
	}

	ValidationResult VoxelWorld::ReadOriginalSample(
		SampleCoord coordinate,
		VoxelSample& sample) const noexcept
	{
		return ReadSample(coordinate, sample, true);
	}

	ValidationResult VoxelWorld::ReadCurrentSample(
		SampleCoord coordinate,
		VoxelSample& sample) const noexcept
	{
		return ReadSample(coordinate, sample, false);
	}

	ValidationResult VoxelWorld::WriteOriginalAndCurrentSample(
		SampleCoord coordinate,
		VoxelSample input,
		bool& changed)
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end() &&
			prepared == DefaultVoxelSample)
		{
			changed = false;
			return {};
		}

		if (iterator != m_Chunks.end())
		{
			VoxelSample original{};
			VoxelSample current{};
			const ValidationResult originalResult =
				iterator->second->ReadOriginalSample(
					address.m_Local,
					original);
			const ValidationResult currentResult =
				iterator->second->ReadCurrentSample(
					address.m_Local,
					current);
			if (originalResult.Failed())
			{
				return originalResult;
			}
			if (currentResult.Failed())
			{
				return currentResult;
			}
			if (original == prepared && current == prepared)
			{
				changed = false;
				return {};
			}
		}

		if (m_WorldVoxelRevision ==
			std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		VoxelChunk* chunk = nullptr;
		bool allocated = false;
		const ValidationResult allocationResult = FindOrCreateChunk(
			address.m_Owner,
			chunk,
			allocated);
		if (allocationResult.Failed())
		{
			return allocationResult;
		}
		static_cast<void>(allocated);
		bool chunkChanged = false;
		const ValidationResult writeResult =
			chunk->WriteOriginalAndCurrentSample(
				address.m_Local,
				prepared,
				chunkChanged);
		if (writeResult.Failed())
		{
			return writeResult;
		}
		if (!chunkChanged)
		{
			changed = false;
			return {};
		}

		++m_WorldVoxelRevision;
		changed = true;
		return {};
	}

	ValidationResult VoxelWorld::RestoreAll(
		RestoreResult& result)
	{
		return RestoreRegionInternal(m_LogicalSampleBounds, result);
	}

	ValidationResult VoxelWorld::RestoreSampleOwnerChunk(
		ChunkCoord chunk,
		RestoreResult& result)
	{
		if (!m_LogicalDomainMetrics.m_SampleOwnerChunkBounds.Contains(chunk))
		{
			return {
				ValidationError::ChunkOutsideLogicalSampleDomain,
			};
		}

		const SampleAabb region = IntersectSampleOwnerChunk(
			chunk,
			m_Config.m_ChunkCellCount,
			m_LogicalSampleBounds);
		return RestoreRegionInternal(region, result);
	}

	ValidationResult VoxelWorld::RestoreRegion(
		const SampleAabb& region,
		RestoreResult& result)
	{
		if (region.IsEmpty())
		{
			return { ValidationError::EmptySampleBounds };
		}
		if (!m_LogicalSampleBounds.ContainsBounds(region))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		return RestoreRegionInternal(region, result);
	}

	ValidationResult VoxelWorld::WriteCurrentSample(
		SampleCoord coordinate,
		VoxelSample input,
		bool& changed)
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		VoxelSample prepared{};
		const ValidationResult prepareResult =
			PrepareVoxelSampleForStorage(input, prepared);
		if (prepareResult.Failed())
		{
			return prepareResult;
		}

		auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end() &&
			prepared == DefaultVoxelSample)
		{
			changed = false;
			return {};
		}

		if (iterator != m_Chunks.end())
		{
			VoxelSample current{};
			const ValidationResult readResult =
				iterator->second->ReadCurrentSample(
					address.m_Local,
					current);
			if (readResult.Failed())
			{
				return readResult;
			}
			if (current == prepared)
			{
				changed = false;
				return {};
			}
		}

		if (m_WorldVoxelRevision ==
			std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}

		VoxelChunk* chunk = nullptr;
		bool allocated = false;
		const ValidationResult allocationResult = FindOrCreateChunk(
			address.m_Owner,
			chunk,
			allocated);
		if (allocationResult.Failed())
		{
			return allocationResult;
		}
		static_cast<void>(allocated);
		bool chunkChanged = false;
		const ValidationResult writeResult =
			chunk->WriteCurrentSample(
				address.m_Local,
				prepared,
				chunkChanged);
		if (writeResult.Failed())
		{
			return writeResult;
		}
		if (!chunkChanged)
		{
			changed = false;
			return {};
		}

		++m_WorldVoxelRevision;
		changed = true;
		return {};
	}

	ValidationResult VoxelWorld::ResolveLogicalSample(
		SampleCoord coordinate,
		OwnedSampleAddress& address) const noexcept
	{
		if (!m_LogicalSampleBounds.Contains(coordinate))
		{
			return { ValidationError::SampleOutsideLogicalBounds };
		}

		return ResolveSampleOwner(
			coordinate,
			m_Config.m_ChunkCellCount,
			address);
	}

	ValidationResult VoxelWorld::ReadSample(
		SampleCoord coordinate,
		VoxelSample& sample,
		bool original) const noexcept
	{
		OwnedSampleAddress address{};
		const ValidationResult addressResult =
			ResolveLogicalSample(coordinate, address);
		if (addressResult.Failed())
		{
			return addressResult;
		}

		const auto iterator = m_Chunks.find(address.m_Owner);
		if (iterator == m_Chunks.end())
		{
			sample = DefaultVoxelSample;
			return {};
		}

		return original
			? iterator->second->ReadOriginalSample(address.m_Local, sample)
			: iterator->second->ReadCurrentSample(address.m_Local, sample);
	}

	ValidationResult VoxelWorld::FindOrCreateChunk(
		ChunkCoord coordinate,
		VoxelChunk*& chunk,
		bool& allocated)
	{
		const auto existing = m_Chunks.find(coordinate);
		if (existing != m_Chunks.end())
		{
			chunk = existing->second.get();
			allocated = false;
			return {};
		}

		std::unique_ptr<VoxelChunk> created;
		const ValidationResult createResult = VoxelChunk::Create(
			m_Config.m_ChunkCellCount,
			created);
		if (createResult.Failed())
		{
			return createResult;
		}

		const auto [iterator, inserted] = m_Chunks.try_emplace(
			coordinate,
			std::move(created));
		chunk = iterator->second.get();
		allocated = inserted;
		return {};
	}

	ValidationResult VoxelWorld::RestoreRegionInternal(
		const SampleAabb& region,
		RestoreResult& result)
	{
		using ChunkRestoreMap = std::map<
			ChunkCoord,
			std::vector<LocalCoord>,
			ChunkCoordZYXLess>;

		RestoreResult restored{};
		ChunkRestoreMap chunkRestores;

		for (std::int64_t z = region.m_Min.m_Z;
			z < region.m_MaxExclusive.m_Z;
			++z)
		{
			for (std::int64_t y = region.m_Min.m_Y;
				y < region.m_MaxExclusive.m_Y;
				++y)
			{
				for (std::int64_t x = region.m_Min.m_X;
					x < region.m_MaxExclusive.m_X;
					++x)
				{
					const SampleCoord coordinate{
						static_cast<std::int32_t>(x),
						static_cast<std::int32_t>(y),
						static_cast<std::int32_t>(z),
					};
					OwnedSampleAddress address{};
					const ValidationResult addressResult =
						ResolveSampleOwner(
							coordinate,
							m_Config.m_ChunkCellCount,
							address);
					if (addressResult.Failed())
					{
						return addressResult;
					}

					const auto iterator = m_Chunks.find(address.m_Owner);
					if (iterator == m_Chunks.end())
					{
						continue;
					}

					VoxelSample original{};
					VoxelSample current{};
					const ValidationResult originalResult =
						iterator->second->ReadOriginalSample(
							address.m_Local,
							original);
					if (originalResult.Failed())
					{
						return originalResult;
					}
					const ValidationResult currentResult =
						iterator->second->ReadCurrentSample(
							address.m_Local,
							current);
					if (currentResult.Failed())
					{
						return currentResult;
					}
					if (original == current)
					{
						continue;
					}

					restored.m_ChangedSampleCoordinates.push_back(
						coordinate);
					chunkRestores[address.m_Owner].push_back(
						address.m_Local);
				}
			}
		}

		if (!restored.Changed())
		{
			result = std::move(restored);
			return {};
		}

		if (m_WorldVoxelRevision ==
			std::numeric_limits<std::uint64_t>::max())
		{
			return { ValidationError::ArithmeticOverflow };
		}
		for (const auto& [chunkCoordinate, coordinates] : chunkRestores)
		{
			static_cast<void>(coordinates);
			const VoxelChunk* const chunk =
				m_Chunks.find(chunkCoordinate)->second.get();
			if (chunk->GetVoxelRevision() ==
				std::numeric_limits<std::uint64_t>::max())
			{
				return { ValidationError::ArithmeticOverflow };
			}
		}

		for (const auto& [chunkCoordinate, coordinates] : chunkRestores)
		{
			VoxelChunk* const chunk =
				m_Chunks.find(chunkCoordinate)->second.get();
			bool chunkChanged = false;
			const ValidationResult restoreResult =
				chunk->RestoreCurrentSamples(
					std::span<const LocalCoord>{ coordinates },
					chunkChanged);
			if (restoreResult.Failed())
			{
				return restoreResult;
			}
			static_cast<void>(chunkChanged);
		}

		++m_WorldVoxelRevision;
		result = std::move(restored);
		return {};
	}
}
