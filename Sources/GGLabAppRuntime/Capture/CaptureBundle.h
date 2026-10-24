#pragma once
#include "Capture/CaptureProductEncoder.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	// Version 3 adds frozen parameters and the parameter-frames range product.
	inline constexpr uint32_t CaptureBundleSchemaVersion = 3;

	struct CaptureBundleDelivery
	{
		uint32_t m_ProductIndex = 0;
		std::optional<uint32_t> m_GraphProductIndex;
		CaptureProductEncodeInput m_Input;
	};

	struct CaptureBundleInput
	{
		std::string m_RunIdentity;
		// The serialized process environment record (SerializeCaptureEnvironment).
		std::string m_Environment;
		uint64_t m_JobId = 0;
		uint64_t m_Attempt = 0;
		uint32_t m_ProductCount = 0;
		std::vector<CaptureBundleDelivery> m_Deliveries;
	};

	struct CaptureBundleProductFiles
	{
		uint32_t m_ProductIndex = 0;
		std::vector<size_t> m_FileIndices;
	};

	// Sequence-owned immutable CPU data. It never enters a SameFrame CapturePlan.
	struct SequenceArtifactBundleInput
	{
		std::string m_RunIdentity;
		std::string m_Environment;
		uint64_t m_SequenceId = 0;
		std::string m_TimingJson;
		std::string m_ParameterFramesJson;
	};

	struct CaptureEncodedBundle
	{
		CapturePublicationContent m_Content;
		std::string m_BundleId;
		std::vector<CaptureBundleProductFiles> m_Products;
		std::string m_Failure;
	};

	// A random 128-bit run identity qualifies process-local job/attempt IDs.
	// Failure returns empty; persisted identity never falls back to a clock/hash.
	[[nodiscard]] std::string CreateCaptureRunIdentity() noexcept;
	[[nodiscard]] std::string MakeCaptureBundleId(std::string_view runIdentity,
		uint64_t jobId, uint64_t attempt);
	[[nodiscard]] std::string MakeSequenceTimingBundleId(
		std::string_view runIdentity, uint64_t sequenceId);
	[[nodiscard]] std::string MakeSequenceArtifactBundleId(
		std::string_view runIdentity, uint64_t sequenceId, bool parameters);
	[[nodiscard]] CaptureEncodedBundle EncodeCaptureBundle(const CaptureBundleInput& input,
		const FrameCaptureImageEncoder& imageEncoder, std::atomic<CaptureWriteStage>& stage) noexcept;
	[[nodiscard]] CaptureEncodedBundle EncodeSequenceArtifactBundle(
		const SequenceArtifactBundleInput& input) noexcept;
}
