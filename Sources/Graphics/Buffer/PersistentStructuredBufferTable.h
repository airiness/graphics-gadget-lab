#pragma once

#include <algorithm>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace gglab
{
	template<typename Key, typename T>
	class PersistentStructuredBufferTable
	{
	public:
		static_assert(std::is_trivially_copyable_v<T>);
		static constexpr uint32_t InvalidSlot = std::numeric_limits<uint32_t>::max();

		struct DirtyRange
		{
			uint32_t m_FirstElement = 0;
			uint32_t m_ElementCount = 0;
		};

		struct BufferVersionStats
		{
			uint64_t m_LastUploadBytes = 0;
			uint64_t m_TotalUploadBytes = 0;
			uint32_t m_LastUploadRangeCount = 0;
		};

		explicit PersistentStructuredBufferTable(
			uint32_t capacity,
			uint32_t bufferCount) :
			m_Data(capacity),
			m_Revisions(capacity, 1),
			m_KeysBySlot(capacity),
			m_UploadedRevisions(bufferCount, std::vector<uint64_t>(capacity, 0)),
			m_BufferVersionStats(bufferCount),
			m_NextRevision(1)
		{
			m_FreeSlots.reserve(capacity);
			for (uint32_t slot = capacity; slot > 0; --slot)
			{
				m_FreeSlots.push_back(slot - 1);
			}
		}

		void BeginUpdate() noexcept
		{
			++m_UpdateSerial;
			if (m_UpdateSerial == 0)
			{
				m_UpdateSerial = 1;
				for (auto& [key, record] : m_Records)
				{
					record.m_LastSeenUpdate = 0;
				}
			}
		}

		[[nodiscard]] uint32_t Upsert(const Key& key, const T& data) noexcept
		{
			auto iter = m_Records.find(key);
			if (iter == m_Records.end())
			{
				if (m_FreeSlots.empty())
				{
					GGLAB_LOG_GRAPHICS_ERROR("PersistentStructuredBufferTable capacity exhausted.");
					return InvalidSlot;
				}

				const uint32_t slot = m_FreeSlots.back();
				m_FreeSlots.pop_back();
				iter = m_Records.emplace(key, Record{ slot, m_UpdateSerial }).first;
				m_KeysBySlot[slot] = key;
				m_Data[slot] = data;
				m_Revisions[slot] = NextRevision();
			}
			else
			{
				iter->second.m_LastSeenUpdate = m_UpdateSerial;
				const uint32_t slot = iter->second.m_Slot;
				if (std::memcmp(&m_Data[slot], &data, sizeof(T)) != 0)
				{
					m_Data[slot] = data;
					m_Revisions[slot] = NextRevision();
				}
			}

			return iter->second.m_Slot;
		}

		void EndUpdate() noexcept
		{
			for (auto iter = m_Records.begin(); iter != m_Records.end();)
			{
				if (iter->second.m_LastSeenUpdate == m_UpdateSerial)
				{
					++iter;
					continue;
				}

				const uint32_t slot = iter->second.m_Slot;
				m_Data[slot] = {};
				m_Revisions[slot] = NextRevision();
				m_KeysBySlot[slot].reset();
				m_FreeSlots.push_back(slot);
				iter = m_Records.erase(iter);
			}
		}

		[[nodiscard]] std::vector<DirtyRange> BuildDirtyRanges(uint32_t bufferIndex) const
		{
			return BuildDirtyRangesInternal(bufferIndex, false);
		}

		[[nodiscard]] std::vector<DirtyRange> BuildDirtyRangesIncludingFreeSlots(uint32_t bufferIndex) const
		{
			return BuildDirtyRangesInternal(bufferIndex, true);
		}

	private:
		[[nodiscard]] std::vector<DirtyRange> BuildDirtyRangesInternal(
			uint32_t bufferIndex,
			bool includeFreeSlots) const
		{
			GGLAB_ASSERT(bufferIndex < m_UploadedRevisions.size());
			std::vector<DirtyRange> ranges;
			if (bufferIndex >= m_UploadedRevisions.size())
			{
				return ranges;
			}

			const auto& uploaded = m_UploadedRevisions[bufferIndex];
			for (uint32_t slot = 0; slot < m_Data.size(); ++slot)
			{
				if ((!includeFreeSlots && !m_KeysBySlot[slot].has_value()) ||
					uploaded[slot] == m_Revisions[slot])
				{
					continue;
				}

				if (!ranges.empty() &&
					ranges.back().m_FirstElement + ranges.back().m_ElementCount == slot)
				{
					++ranges.back().m_ElementCount;
				}
				else
				{
					ranges.push_back({ slot, 1 });
				}
			}
			return ranges;
		}
	public:
		[[nodiscard]] std::span<const T> GetData(const DirtyRange& range) const noexcept
		{
			GGLAB_ASSERT(range.m_FirstElement <= m_Data.size() &&
				range.m_ElementCount <= m_Data.size() - range.m_FirstElement);
			return std::span<const T>(m_Data).subspan(range.m_FirstElement, range.m_ElementCount);
		}

		void Commit(uint32_t bufferIndex, std::span<const DirtyRange> ranges) noexcept
		{
			GGLAB_ASSERT(bufferIndex < m_UploadedRevisions.size());
			if (bufferIndex >= m_UploadedRevisions.size())
			{
				return;
			}

			auto& uploaded = m_UploadedRevisions[bufferIndex];
			auto& stats = m_BufferVersionStats[bufferIndex];
			stats.m_LastUploadBytes = 0;
			stats.m_LastUploadRangeCount = static_cast<uint32_t>(ranges.size());
			for (const DirtyRange& range : ranges)
			{
				stats.m_LastUploadBytes += static_cast<uint64_t>(range.m_ElementCount) * sizeof(T);
				for (uint32_t slot = range.m_FirstElement;
					slot < range.m_FirstElement + range.m_ElementCount;
					++slot)
				{
					uploaded[slot] = m_Revisions[slot];
				}
			}
			stats.m_TotalUploadBytes += stats.m_LastUploadBytes;
		}

		[[nodiscard]] uint32_t GetLiveCount() const noexcept
		{
			return static_cast<uint32_t>(m_Records.size());
		}
		[[nodiscard]] uint32_t GetCapacity() const noexcept { return static_cast<uint32_t>(m_Data.size()); }
		[[nodiscard]] uint32_t GetBufferCount() const noexcept { return static_cast<uint32_t>(m_UploadedRevisions.size()); }
		[[nodiscard]] uint32_t GetFreeCount() const noexcept { return static_cast<uint32_t>(m_FreeSlots.size()); }
		[[nodiscard]] uint64_t GetUpdateSerial() const noexcept { return m_UpdateSerial; }
		[[nodiscard]] bool IsOccupied(uint32_t slot) const noexcept
		{
			return slot < m_KeysBySlot.size() && m_KeysBySlot[slot].has_value();
		}
		[[nodiscard]] const std::optional<Key>& GetKey(uint32_t slot) const noexcept
		{
			GGLAB_ASSERT(slot < m_KeysBySlot.size());
			return m_KeysBySlot[slot];
		}
		[[nodiscard]] uint64_t GetRevision(uint32_t slot) const noexcept
		{
			return slot < m_Revisions.size() ? m_Revisions[slot] : 0;
		}
		[[nodiscard]] uint64_t GetUploadedRevision(uint32_t bufferIndex, uint32_t slot) const noexcept
		{
			GGLAB_ASSERT(bufferIndex < m_UploadedRevisions.size() && slot < m_Data.size());
			return bufferIndex < m_UploadedRevisions.size() && slot < m_Data.size() ?
				m_UploadedRevisions[bufferIndex][slot] : 0;
		}
		[[nodiscard]] const BufferVersionStats& GetBufferVersionStats(uint32_t bufferIndex) const noexcept
		{
			GGLAB_ASSERT(bufferIndex < m_BufferVersionStats.size());
			return m_BufferVersionStats[bufferIndex];
		}

	private:
		struct Record
		{
			uint32_t m_Slot = InvalidSlot;
			uint64_t m_LastSeenUpdate = 0;
		};

		uint64_t NextRevision() noexcept
		{
			++m_NextRevision;
			if (m_NextRevision == 0)
			{
				m_NextRevision = 1;
				for (auto& revisions : m_UploadedRevisions)
				{
					std::fill(revisions.begin(), revisions.end(), 0);
				}
			}
			return m_NextRevision;
		}

		std::unordered_map<Key, Record> m_Records;
		std::vector<T> m_Data;
		std::vector<uint64_t> m_Revisions;
		std::vector<std::optional<Key>> m_KeysBySlot;
		std::vector<uint32_t> m_FreeSlots;
		std::vector<std::vector<uint64_t>> m_UploadedRevisions;
		std::vector<BufferVersionStats> m_BufferVersionStats;
		uint64_t m_UpdateSerial = 0;
		uint64_t m_NextRevision = 0;
	};
}
