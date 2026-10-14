#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace gglab
{
	namespace
	{
		uint32_t OrderedFloat(uint32_t bits) noexcept
		{
			return bits ^ ((bits & 0x80000000u) ? 0xffffffffu : 0x80000000u);
		}
		float FromOrderedFloat(uint32_t value) noexcept
		{
			return std::bit_cast<float>(value ^ ((value & 0x80000000u) ? 0x80000000u : 0xffffffffu));
		}
		bool ValidPolicy(const NonFiniteClampPolicy& policy) noexcept
		{
			switch (policy.m_Kind)
			{
			case NonFiniteClampPolicyKind::Report:
			case NonFiniteClampPolicyKind::Error:
			case NonFiniteClampPolicyKind::Expected: return true;
			case NonFiniteClampPolicyKind::Budgeted:
				return std::isfinite(policy.m_MaxMagnitude) && policy.m_MaxMagnitude >= 0.0f;
			}
			return false;
		}
		bool ExceedsPolicy(uint64_t count, float magnitude, const NonFiniteClampPolicy& policy) noexcept
		{
			if (count == 0)
			{
				return false;
			}
			return policy.m_Kind == NonFiniteClampPolicyKind::Error ||
				(policy.m_Kind == NonFiniteClampPolicyKind::Budgeted &&
					(count > policy.m_MaxCount || magnitude > policy.m_MaxMagnitude));
		}
	}

	NonFiniteScanDispatch GetNonFiniteScanDispatch(uint32_t scalarCount) noexcept
	{
		if (scalarCount == 0)
		{
			return {};
		}
		const uint32_t groups = scalarCount / 64 + (scalarCount % 64 != 0);
		const uint32_t x = std::min(groups, 65535u);
		return { x, groups / x + (groups % x != 0) };
	}

	NonFiniteBufferScanLayout BuildNonFiniteBufferScanLayout(GpuBufferLayout layout,
		uint64_t bufferBytes, std::optional<uint64_t> validElementCount) noexcept
	{
		NonFiniteBufferScanLayout result;
		const auto fail = [&](NonFiniteScanLayoutFailure failure)
			{ result.m_Failure = failure; result.m_Components.clear(); return result; };
		if (!validElementCount)
		{
			return fail(NonFiniteScanLayoutFailure::MissingValidElementCount);
		}
		// ByteAddressBuffer loads are 32-bit aligned, including the word containing a half.
		if (layout.m_Stride == 0 || layout.m_Stride % 4 != 0 || bufferBytes % layout.m_Stride != 0 ||
			*validElementCount > bufferBytes / layout.m_Stride)
		{
			return fail(NonFiniteScanLayoutFailure::InvalidLayout);
		}
		std::vector<std::pair<uint32_t, uint32_t>> ranges;
		for (const auto& field : layout.m_Fields)
		{
			uint32_t size = 0;
			switch (field.m_Scalar)
			{
			case GpuBufferScalar::Uint8: size = 1; break;
			case GpuBufferScalar::Float16: size = 2; break;
			case GpuBufferScalar::Uint32:
			case GpuBufferScalar::Float32: size = 4; break;
			default: return fail(NonFiniteScanLayoutFailure::InvalidLayout);
			}
			if (!field.m_Name || field.m_Name[0] == '\0' || field.m_Count == 0 ||
				field.m_Offset >= layout.m_Stride || field.m_Offset % size != 0 ||
				field.m_Count > (layout.m_Stride - field.m_Offset) / size)
			{
				return fail(NonFiniteScanLayoutFailure::InvalidLayout);
			}
			ranges.emplace_back(field.m_Offset, field.m_Offset + field.m_Count * size);
			if (field.m_Scalar == GpuBufferScalar::Float16 || field.m_Scalar == GpuBufferScalar::Float32)
			{
				for (uint32_t component = 0; component < field.m_Count; ++component)
				{
					result.m_Components.push_back({ field.m_Offset + component * size, size });
				}
			}
		}
		std::ranges::sort(ranges);
		for (size_t i = 1; i < ranges.size(); ++i)
		{
			if (ranges[i].first < ranges[i - 1].second)
			{
				return fail(NonFiniteScanLayoutFailure::InvalidLayout);
			}
		}
		if (result.m_Components.empty())
		{
			return fail(NonFiniteScanLayoutFailure::NoFloatFields);
		}
		if (*validElementCount > NonFiniteNoIndex / result.m_Components.size() ||
			// ByteAddressBuffer addresses must not wrap, even for a small scalar count.
			*validElementCount > (uint64_t{ NonFiniteNoIndex } + 1) / layout.m_Stride)
		{
			return fail(NonFiniteScanLayoutFailure::TooLarge);
		}
		std::ranges::sort(result.m_Components, {}, &NonFiniteBufferComponent::m_Offset);
		result.m_Stride = layout.m_Stride;
		result.m_ValidElementCount = static_cast<uint32_t>(*validElementCount);
		result.m_ScalarCount = static_cast<uint32_t>(*validElementCount * result.m_Components.size());
		return result;
	}

	std::optional<uint32_t> GetNonFiniteTextureScalarCount(
		uint32_t width, uint32_t height, uint32_t slices, uint32_t channels) noexcept
	{
		if (channels == 0 || channels > 4 || width == 0 || height == 0 || slices == 0)
		{
			return std::nullopt;
		}
		uint32_t count = channels;
		for (const auto extent : { width, height, slices })
		{
			if (extent > NonFiniteNoIndex / count)
			{
				return std::nullopt;
			}
			count *= extent;
		}
		return count;
	}

	std::optional<NonFiniteTextureLocation> GetNonFiniteTextureLocation(
		uint32_t index, uint32_t width, uint32_t height, uint32_t slices, uint32_t channels) noexcept
	{
		const auto count = GetNonFiniteTextureScalarCount(width, height, slices, channels);
		if (!count || index >= *count)
		{
			return std::nullopt;
		}
		NonFiniteTextureLocation location;
		location.m_Channel = index % channels;
		index /= channels;
		location.m_X = index % width;
		index /= width;
		location.m_Y = index % height;
		location.m_Slice = index / height;
		return location;
	}

	void AccumulateNonFiniteScanValue(NonFiniteScanRecord& record, uint32_t bits, uint32_t index) noexcept
	{
		++record.m_CheckedCount;
		if ((bits & 0x7f800000u) == 0x7f800000u)
		{
			if (bits & 0x007fffffu)
			{
				++record.m_NanCount;
			}
			else if (bits & 0x80000000u)
			{
				++record.m_NegativeInfCount;
			}
			else
			{
				++record.m_PositiveInfCount;
			}
			record.m_FirstNonFiniteIndex = std::min(record.m_FirstNonFiniteIndex, index);
			return;
		}
		++record.m_FiniteCount;
		const auto ordered = OrderedFloat(bits);
		record.m_MinimumOrdered = std::min(record.m_MinimumOrdered, ordered);
		record.m_MaximumOrdered = std::max(record.m_MaximumOrdered, ordered);
	}

	NonFiniteBoundaryResult EvaluateNonFiniteBoundary(
		const std::optional<NonFiniteScanRecord>& record, uint32_t expectedScalarCount) noexcept
	{
		NonFiniteBoundaryResult result;
		if (!record || record->m_CheckedCount != expectedScalarCount)
		{
			return result;
		}
		const auto& value = *record;
		const uint64_t anomalies = uint64_t{ value.m_NanCount } + value.m_PositiveInfCount + value.m_NegativeInfCount;
		if (anomalies + value.m_FiniteCount != expectedScalarCount ||
			(anomalies == 0 ? value.m_FirstNonFiniteIndex != NonFiniteNoIndex
				: value.m_FirstNonFiniteIndex >= expectedScalarCount))
		{
			return result;
		}
		if (value.m_FiniteCount != 0)
		{
			const float minimum = FromOrderedFloat(value.m_MinimumOrdered);
			const float maximum = FromOrderedFloat(value.m_MaximumOrdered);
			if (value.m_MinimumOrdered > value.m_MaximumOrdered || !std::isfinite(minimum) || !std::isfinite(maximum))
			{
				return result;
			}
			result.m_Minimum = minimum;
			result.m_Maximum = maximum;
		}
		else if (value.m_MinimumOrdered != NonFiniteNoIndex || value.m_MaximumOrdered != 0)
		{
			return result;
		}
		if (anomalies != 0)
		{
			result.m_FirstNonFiniteIndex = value.m_FirstNonFiniteIndex;
			result.m_Claim = { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteDetected };
		}
		else
		{
			result.m_Claim = { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
		}
		return result;
	}

	NonFiniteClaimResult EvaluateNonFiniteSite(bool instrumentationAvailable,
		const std::optional<NonFiniteSiteCounters>& counters, const NonFiniteSitePolicy& policy) noexcept
	{
		if (!instrumentationAvailable)
		{
			return { NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::MissingCoverage };
		}
		if (!counters || !ValidPolicy(policy.m_Negative) || !ValidPolicy(policy.m_Fp16Maximum))
		{
			return {};
		}
		const auto& value = *counters;
		const auto validExtreme = [](uint64_t count, const std::optional<float>& extreme, bool negative)
			{
				return count == 0 ? !extreme : extreme && std::isfinite(*extreme) &&
					(negative ? *extreme < 0.0f : *extreme > NonFiniteFp16Maximum);
			};
		if (!validExtreme(value.m_NegativeClamps, value.m_MostNegative, true) ||
			!validExtreme(value.m_Fp16MaximumClamps, value.m_Maximum, false))
		{
			return {};
		}
		// Even an expected/report-only finite-clamp policy never excuses a replacement.
		if (value.m_NonFiniteReplacements != 0)
		{
			return { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::NonFiniteReplaced };
		}
		if (ExceedsPolicy(value.m_NegativeClamps, -value.m_MostNegative.value_or(0.0f), policy.m_Negative) ||
			ExceedsPolicy(value.m_Fp16MaximumClamps, value.m_Maximum.value_or(0.0f), policy.m_Fp16Maximum))
		{
			return { NonFiniteClaimStatus::Failed, NonFiniteWatchFailure::ClampPolicyExceeded };
		}
		return { NonFiniteClaimStatus::Passed, NonFiniteWatchFailure::None };
	}
}
