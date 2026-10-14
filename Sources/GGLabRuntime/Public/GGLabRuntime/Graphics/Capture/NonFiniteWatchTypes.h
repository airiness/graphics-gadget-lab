#pragma once
#include "GGLabRuntime/Graphics/Resource/GpuBufferLayout.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObservation.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

namespace gglab
{
	inline constexpr uint32_t NonFiniteNoIndex = std::numeric_limits<uint32_t>::max();
	inline constexpr float NonFiniteFp16Maximum = 65504.0f;

	// Shader ABI: Common/NonFiniteScan.hlsli. Counts are scalar components, not pixels.
	// Admission must bound the scalar count to UINT32_MAX before dispatch.
	struct NonFiniteScanRecord
	{
		uint32_t m_CheckedCount = 0;
		uint32_t m_NanCount = 0;
		uint32_t m_PositiveInfCount = 0;
		uint32_t m_NegativeInfCount = 0;
		uint32_t m_FiniteCount = 0;
		uint32_t m_MinimumOrdered = NonFiniteNoIndex;
		uint32_t m_MaximumOrdered = 0;
		uint32_t m_FirstNonFiniteIndex = NonFiniteNoIndex;
	};
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
	static_assert(sizeof(NonFiniteScanRecord) == 32);
	static_assert(offsetof(NonFiniteScanRecord, m_CheckedCount) == 0);
	static_assert(offsetof(NonFiniteScanRecord, m_NanCount) == 4);
	static_assert(offsetof(NonFiniteScanRecord, m_PositiveInfCount) == 8);
	static_assert(offsetof(NonFiniteScanRecord, m_NegativeInfCount) == 12);
	static_assert(offsetof(NonFiniteScanRecord, m_FiniteCount) == 16);
	static_assert(offsetof(NonFiniteScanRecord, m_MinimumOrdered) == 20);
	static_assert(offsetof(NonFiniteScanRecord, m_MaximumOrdered) == 24);
	static_assert(offsetof(NonFiniteScanRecord, m_FirstNonFiniteIndex) == 28);

	struct NonFiniteScanParameters
	{
		uint32_t m_ScalarCount = 0;
		uint32_t m_DispatchGroupsX = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		uint32_t m_Channels = 0;
		uint32_t m_TextureIndex = 0;
		uint32_t m_BufferStride = 0;
		uint32_t m_ComponentCount = 0;
	};
	static_assert(sizeof(NonFiniteScanParameters) == 32);
	static_assert(offsetof(NonFiniteScanParameters, m_ScalarCount) == 0);
	static_assert(offsetof(NonFiniteScanParameters, m_DispatchGroupsX) == 4);
	static_assert(offsetof(NonFiniteScanParameters, m_Width) == 8);
	static_assert(offsetof(NonFiniteScanParameters, m_Height) == 12);
	static_assert(offsetof(NonFiniteScanParameters, m_Channels) == 16);
	static_assert(offsetof(NonFiniteScanParameters, m_TextureIndex) == 20);
	static_assert(offsetof(NonFiniteScanParameters, m_BufferStride) == 24);
	static_assert(offsetof(NonFiniteScanParameters, m_ComponentCount) == 28);
	struct NonFiniteScanDispatch
	{
		uint32_t m_X = 0;
		uint32_t m_Y = 0;
	};
	[[nodiscard]] NonFiniteScanDispatch GetNonFiniteScanDispatch(uint32_t scalarCount) noexcept;

	struct NonFiniteBufferComponent
	{
		uint32_t m_Offset = 0;
		// 2 or 4 bytes; integers and padding never enter this list.
		uint32_t m_Size = 4;
	};
	static_assert(sizeof(NonFiniteBufferComponent) == 8);
	static_assert(offsetof(NonFiniteBufferComponent, m_Offset) == 0);
	static_assert(offsetof(NonFiniteBufferComponent, m_Size) == 4);

	enum class NonFiniteScanLayoutFailure : uint8_t
	{
		None, InvalidLayout, MissingValidElementCount, NoFloatFields, TooLarge,
	};
	struct NonFiniteBufferScanLayout
	{
		NonFiniteScanLayoutFailure m_Failure = NonFiniteScanLayoutFailure::None;
		uint32_t m_Stride = 0;
		uint32_t m_ValidElementCount = 0;
		uint32_t m_ScalarCount = 0;
		// Sorted by byte offset, so the first anomaly is independent of registry order.
		std::vector<NonFiniteBufferComponent> m_Components;
	};
	[[nodiscard]] NonFiniteBufferScanLayout BuildNonFiniteBufferScanLayout(GpuBufferLayout layout,
		uint64_t bufferBytes, std::optional<uint64_t> validElementCount) noexcept;
	[[nodiscard]] std::optional<uint32_t> GetNonFiniteTextureScalarCount(
		uint32_t width, uint32_t height, uint32_t slices, uint32_t channels) noexcept;

	struct NonFiniteTextureLocation
	{
		uint32_t m_X = 0;
		uint32_t m_Y = 0;
		uint32_t m_Slice = 0;
		uint32_t m_Channel = 0;
	};
	[[nodiscard]] std::optional<NonFiniteTextureLocation> GetNonFiniteTextureLocation(
		uint32_t index, uint32_t width, uint32_t height, uint32_t slices, uint32_t channels) noexcept;

	// CPU reference for the scan ABI, also useful for validating synthetic records.
	// Classification uses IEEE bits: an optimizing shader must not fold away NaN/Inf.
	// Like a GPU dispatch, the caller visits each admitted scalar index once.
	void AccumulateNonFiniteScanValue(NonFiniteScanRecord& record, uint32_t ieeeBits, uint32_t index) noexcept;

	enum class NonFiniteClaimStatus : uint8_t { Unavailable, Incomplete, NotApplicable, Passed, Failed };
	enum class NonFiniteWatchFailure : uint8_t
	{
		None, NonFiniteDetected, NonFiniteReplaced, ClampPolicyExceeded, MissingCoverage,
	};
	struct NonFiniteClaimResult
	{
		NonFiniteClaimStatus m_Status = NonFiniteClaimStatus::Incomplete;
		NonFiniteWatchFailure m_Failure = NonFiniteWatchFailure::MissingCoverage;
	};
	struct NonFiniteBoundaryResult
	{
		NonFiniteClaimResult m_Claim;
		std::optional<float> m_Minimum;
		std::optional<float> m_Maximum;
		std::optional<uint32_t> m_FirstNonFiniteIndex;
	};
	// Missing, malformed, uncleared or partial readbacks can never pass.
	[[nodiscard]] NonFiniteBoundaryResult EvaluateNonFiniteBoundary(
		const std::optional<NonFiniteScanRecord>& record, uint32_t expectedScalarCount) noexcept;

	// Explicit low-level boundary request. Buffer interpretations own their float
	// component table; build it from a registered/explicit layout and a producer's
	// valid element count, never from allocated capacity alone.
	struct NonFiniteScanTarget
	{
		RGResourceAddress m_Address;
		NonFiniteBufferScanLayout m_BufferLayout;
	};
	struct NonFiniteScanBoundary
	{
		RGObservationTarget m_Target;
		NonFiniteScanParameters m_Parameters;
		NonFiniteBufferScanLayout m_BufferLayout;
		NonFiniteScanRecord m_Record;
		NonFiniteBoundaryResult m_Result;
	};
	struct NonFiniteScanPayload
	{
		uint64_t m_ConfigurationVersion = 0;
		// Boundary scans cannot establish production sanitization/recovery coverage.
		NonFiniteClaimResult m_Production{ NonFiniteClaimStatus::Unavailable, NonFiniteWatchFailure::MissingCoverage };
		std::vector<NonFiniteScanBoundary> m_Boundaries;
	};

	enum class NonFiniteClampPolicyKind : uint8_t { Report, Error, Expected, Budgeted };
	struct NonFiniteClampPolicy
	{
		NonFiniteClampPolicyKind m_Kind = NonFiniteClampPolicyKind::Report;
		uint64_t m_MaxCount = 0;
		// Absolute original channel magnitude, not the distance from the clamp endpoint.
		float m_MaxMagnitude = 0.0f;
	};
	struct NonFiniteSitePolicy
	{
		NonFiniteClampPolicy m_Negative;
		NonFiniteClampPolicy m_Fp16Maximum;
	};
	struct NonFiniteSiteCounters
	{
		uint64_t m_NonFiniteReplacements = 0;
		uint64_t m_NegativeClamps = 0;
		uint64_t m_Fp16MaximumClamps = 0;
		std::optional<float> m_MostNegative;
		std::optional<float> m_Maximum;
	};
	[[nodiscard]] NonFiniteClaimResult EvaluateNonFiniteSite(bool instrumentationAvailable,
		const std::optional<NonFiniteSiteCounters>& counters, const NonFiniteSitePolicy& policy) noexcept;
}
