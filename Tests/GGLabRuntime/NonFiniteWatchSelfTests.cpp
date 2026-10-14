#include "NonFiniteWatchSelfTests.h"
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/Pipeline/ForwardPlus.h"
#include "GGLabRuntime/Graphics/Shader/ShaderProgramCatalog.h"
#include "GGLabTestCore/SelfTest.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace gglab
{
	void RunNonFiniteWatchSelfTests(SelfTestContext& context) noexcept
	{
		context.Check(std::ranges::none_of(shader_programs::GetRendererInitialShaderProgramDemand(), [](const ShaderProgramRef& program)
			{ return program.m_ProgramId == "gglab.shader.nonfinite-scan"; }),
			"Ordinary renderer startup never demands observation scan programs");
		const auto incomplete = [](const auto& result)
			{ return result.m_Status == NonFiniteClaimStatus::Incomplete && result.m_Failure == NonFiniteWatchFailure::MissingCoverage; };
		context.Check(incomplete(EvaluateNonFiniteBoundary(std::nullopt, 4).m_Claim), "Missing scan evidence is incomplete");
		context.Check(incomplete(EvaluateNonFiniteBoundary(NonFiniteScanRecord{}, 4).m_Claim), "An undispatched record never passes a nonempty scan");
		context.Check(EvaluateNonFiniteBoundary(NonFiniteScanRecord{}, 0).m_Claim.m_Status == NonFiniteClaimStatus::Passed,
			"An explicitly empty valid buffer range checks zero scalars");
		NonFiniteScanRecord record;
		// Deliberately visit out of index order, as GPU waves do. Include both NaN signs,
		// signaling NaN, both infinities, subnormals, signed zero and finite negatives.
		constexpr std::array bits{ 0x7fc00001u, 0x7f800000u, 0xff800000u, 0xffc01234u,
			0x7f800001u, 0x80000000u, 0x00000000u, 0x00000001u, 0xbf800000u, 0x477fe000u };
		for (uint32_t i = static_cast<uint32_t>(bits.size()); i != 0; --i)
		{
			AccumulateNonFiniteScanValue(record, bits[i - 1], i - 1);
		}
		const auto result = EvaluateNonFiniteBoundary(record, static_cast<uint32_t>(bits.size()));
		context.Check(record.m_NanCount == 3 && record.m_PositiveInfCount == 1 && record.m_NegativeInfCount == 1 &&
			record.m_FiniteCount == 5 && record.m_CheckedCount == bits.size(), "IEEE classification retains NaN signs/payloads and counts scalar components");
		context.Check(result.m_Claim.m_Failure == NonFiniteWatchFailure::NonFiniteDetected && result.m_FirstNonFiniteIndex == 0u,
			"First anomaly is the minimum linear index, not wave completion order");
		context.Check(result.m_Minimum == -1.0f && result.m_Maximum == 65504.0f, "Finite extrema exclude all non-finite values");
		NonFiniteScanRecord zeros;
		AccumulateNonFiniteScanValue(zeros, 0u, 1);
		AccumulateNonFiniteScanValue(zeros, 0x80000000u, 0);
		const auto zeroResult = EvaluateNonFiniteBoundary(zeros, 2);
		context.Check(zeroResult.m_Minimum && std::signbit(*zeroResult.m_Minimum) && zeroResult.m_Maximum && !std::signbit(*zeroResult.m_Maximum),
			"Integer ordered extrema preserve negative and positive zero");
		NonFiniteScanRecord allInvalid;
		AccumulateNonFiniteScanValue(allInvalid, 0x7f800000u, 0);
		const auto invalidResult = EvaluateNonFiniteBoundary(allInvalid, 1);
		context.Check(!invalidResult.m_Minimum && !invalidResult.m_Maximum && invalidResult.m_FirstNonFiniteIndex == 0u,
			"All-non-finite scans have null finite extrema");
		auto malformed = record;
		malformed.m_NanCount = NonFiniteNoIndex;
		context.Check(incomplete(EvaluateNonFiniteBoundary(malformed, 10).m_Claim), "Counter validation cannot wrap into a pass");
		malformed = record;
		malformed.m_FirstNonFiniteIndex = NonFiniteNoIndex;
		context.Check(incomplete(EvaluateNonFiniteBoundary(malformed, 10).m_Claim), "Anomaly counts require an in-range location");
		malformed = record;
		malformed.m_MinimumOrdered = 0xffffffffu;
		context.Check(incomplete(EvaluateNonFiniteBoundary(malformed, 10).m_Claim), "Uninitialized extrema cannot pass");
		context.Check(incomplete(EvaluateNonFiniteBoundary(record, 11).m_Claim), "Partial readback coverage is incomplete");

		context.Check(GetNonFiniteTextureScalarCount(2, 3, 6, 4) == 144u &&
			!GetNonFiniteTextureScalarCount(NonFiniteNoIndex, 2, 1, 4) &&
			!GetNonFiniteTextureScalarCount(2, 3, 6, 0) && !GetNonFiniteTextureScalarCount(0, 3, 6, 4),
			"Texture scan admission bounds channel count and 32-bit indexing");
		const auto location = GetNonFiniteTextureLocation(143, 2, 3, 6, 4);
		context.Check(location && location->m_X == 1 && location->m_Y == 2 && location->m_Slice == 5 && location->m_Channel == 3 &&
			!GetNonFiniteTextureLocation(144, 2, 3, 6, 4), "Texture anomaly decodes x/y/slice/channel in C order");
		const auto dispatch = GetNonFiniteScanDispatch(NonFiniteNoIndex);
		context.Check(GetNonFiniteScanDispatch(0).m_X == 0 && GetNonFiniteScanDispatch(65).m_X == 2 &&
			dispatch.m_X <= 65535 && dispatch.m_Y <= 65535 && uint64_t{ dispatch.m_X } * dispatch.m_Y * 64 >= NonFiniteNoIndex,
			"Large scans use a bounded rectangular dispatch without rounding overflow");

		constexpr std::array fields{
			GpuBufferFieldLayout{ "flags", 0, GpuBufferScalar::Uint32 },
			GpuBufferFieldLayout{ "halfPair", 12, GpuBufferScalar::Float16, 2 },
			GpuBufferFieldLayout{ "position", 4, GpuBufferScalar::Float32, 2 },
		};
		const auto layout = BuildNonFiniteBufferScanLayout({ 20, fields }, 2000, 3);
		context.Check(layout.m_Failure == NonFiniteScanLayoutFailure::None && layout.m_ScalarCount == 12 &&
			layout.m_ValidElementCount == 3 && layout.m_Components.size() == 4 && layout.m_Components[0].m_Offset == 4 &&
			layout.m_Components[2].m_Offset == 12 && layout.m_Components[3].m_Size == 2,
			"Only registered float fields in the producer's valid range enter the scan, sorted by offset");
		context.Check(BuildNonFiniteBufferScanLayout({ 20, fields }, 2000, std::nullopt).m_Failure == NonFiniteScanLayoutFailure::MissingValidElementCount,
			"Unused buffer capacity is never implicitly scanned");
		context.Check(BuildNonFiniteBufferScanLayout({ 20, fields }, 2000, 101).m_Failure == NonFiniteScanLayoutFailure::InvalidLayout,
			"Valid element count cannot exceed physical capacity");
		context.Check(BuildNonFiniteBufferScanLayout({ 20, fields }, 2000, 0).m_ScalarCount == 0,
			"A producer can explicitly report zero active elements");
		constexpr std::array overlapping{ GpuBufferFieldLayout{ "a", 0, GpuBufferScalar::Float32 }, GpuBufferFieldLayout{ "b", 2, GpuBufferScalar::Float16 } };
		context.Check(BuildNonFiniteBufferScanLayout({ 4, overlapping }, 4, 1).m_Failure == NonFiniteScanLayoutFailure::InvalidLayout,
			"Overlapping layout fields are rejected instead of double-counted");
		constexpr std::array overflowing{ GpuBufferFieldLayout{ "a", 4, GpuBufferScalar::Float32, NonFiniteNoIndex } };
		context.Check(BuildNonFiniteBufferScanLayout({ 8, overflowing }, 8, 1).m_Failure == NonFiniteScanLayoutFailure::InvalidLayout,
			"Field size arithmetic cannot overflow into an accepted layout");
		context.Check(BuildNonFiniteBufferScanLayout(ForwardPlusTileHeaderLayout, 80, 10).m_Failure == NonFiniteScanLayoutFailure::NoFloatFields &&
			BuildNonFiniteBufferScanLayout(ForwardPlusTileDepthRangeLayout, 80, 10).m_ScalarCount == 20,
			"Actual T2 Forward+ layouts exclude integer headers and include float depth ranges");
		context.Check(BuildNonFiniteBufferScanLayout(ForwardPlusTileDepthRangeLayout, uint64_t{ 1 } << 34, uint64_t{ 1 } << 31).m_Failure == NonFiniteScanLayoutFailure::TooLarge,
			"Oversized buffer scans fail before scalar counters or byte addresses wrap");

		NonFiniteSiteCounters counters;
		NonFiniteSitePolicy policy;
		context.Check(EvaluateNonFiniteSite(false, counters, policy).m_Status == NonFiniteClaimStatus::Unavailable,
			"Without startup instrumentation a clean boundary does not establish production coverage");
		context.Check(incomplete(EvaluateNonFiniteSite(true, std::nullopt, policy)), "Missing applicable site evidence is incomplete");
		counters.m_NegativeClamps = 2;
		counters.m_MostNegative = -0.25f;
		policy.m_Negative.m_Kind = NonFiniteClampPolicyKind::Expected;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Status == NonFiniteClaimStatus::Passed,
			"Expected TAA YCoCg negative rectification clamps do not fail");
		policy.m_Negative.m_Kind = NonFiniteClampPolicyKind::Error;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Failure == NonFiniteWatchFailure::ClampPolicyExceeded,
			"The same finite clamp fails at an error-policy site");
		policy.m_Negative = { NonFiniteClampPolicyKind::Budgeted, 2, 0.25f };
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Status == NonFiniteClaimStatus::Passed, "Clamp budget endpoints are inclusive");
		policy.m_Negative.m_MaxCount = 1;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Failure == NonFiniteWatchFailure::ClampPolicyExceeded, "Clamp count exceeding its budget fails");
		policy.m_Negative.m_MaxCount = 2;
		policy.m_Negative.m_MaxMagnitude = 0.2f;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Failure == NonFiniteWatchFailure::ClampPolicyExceeded, "Clamp magnitude exceeding its budget fails");
		policy.m_Negative.m_Kind = NonFiniteClampPolicyKind::Report;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Status == NonFiniteClaimStatus::Passed, "Undeclared finite clamp policies are report-only");
		counters.m_NonFiniteReplacements = 1;
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Failure == NonFiniteWatchFailure::NonFiniteReplaced,
			"Report-only and expected policies never excuse non-finite replacement");
		counters = { .m_Fp16MaximumClamps = 1, .m_Maximum = 70000.0f };
		policy.m_Fp16Maximum = { NonFiniteClampPolicyKind::Budgeted, 1, 70000.0f };
		context.Check(EvaluateNonFiniteSite(true, counters, policy).m_Status == NonFiniteClaimStatus::Passed, "FP16 clamp budgets use the original finite magnitude");
		counters.m_Maximum = std::numeric_limits<float>::infinity();
		context.Check(incomplete(EvaluateNonFiniteSite(true, counters, policy)), "Non-finite finite-clamp extrema are invalid evidence");
		counters.m_Maximum = NonFiniteFp16Maximum;
		context.Check(incomplete(EvaluateNonFiniteSite(true, counters, policy)), "Unclamped endpoint cannot serve as an extreme for a nonzero clamp count");
		counters = {};
		policy.m_Fp16Maximum.m_MaxMagnitude = std::numeric_limits<float>::quiet_NaN();
		context.Check(incomplete(EvaluateNonFiniteSite(true, counters, policy)), "Invalid budget policy cannot silently pass a clean frame");
	}
}
