#pragma once

#include "Core/StringId.h"

#include <cstdint>
#include <string_view>

namespace gglab
{
	using SnapshotId = uint64_t;

	consteval SnapshotId MakeSnapshotId(std::string_view name) noexcept
	{
		uint64_t crc = std::numeric_limits<uint64_t>::max();
		for (const char character : name)
		{
			const auto byte = static_cast<unsigned char>(character);
			crc = CRC64_TABLE[(crc ^ byte) & 0xFF] ^ (crc >> 8);
		}
		return crc ^ std::numeric_limits<uint64_t>::max();
	}

	enum class SnapshotUpdatePolicy : uint8_t
	{
		EveryFrame,
		OnDemand,
		ManualRefresh,
	};

	template<typename T>
	struct SnapshotTraits;

	template<typename T>
	inline constexpr SnapshotId SnapshotIdOf = SnapshotTraits<T>::Id;

	struct SnapshotProfile
	{
		SnapshotId m_Id = 0;
		std::string_view m_Name;
		SnapshotUpdatePolicy m_Policy = SnapshotUpdatePolicy::OnDemand;
		double m_LastCaptureMilliseconds = 0.0;
		double m_AverageCaptureMilliseconds = 0.0;
		double m_MaxCaptureMilliseconds = 0.0;
		uint64_t m_CaptureCount = 0;
		uint64_t m_CacheHitCount = 0;
		uint64_t m_LastCaptureFrame = 0;
		bool m_HasSnapshot = false;
		bool m_RefreshPending = false;
	};
}
