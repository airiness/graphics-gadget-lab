#pragma once
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	// Version of the persisted capture sidecar JSON. Increment it for every
	// change that renames, removes or reinterprets a field.
	inline constexpr uint32_t FrameCaptureMetadataSchemaVersion = 1;

	enum class FrameCaptureTiming : uint8_t
	{
		// The next frame that records the source tap, including loading frames.
		NextFrame,
		// The first frame after every readiness gate holds for the requested
		// number of settled frames.
		AfterReady,
	};

	[[nodiscard]] constexpr std::string_view GetFrameCaptureTimingName(
		FrameCaptureTiming timing) noexcept
	{
		return timing == FrameCaptureTiming::AfterReady ? "after-ready" : "next-frame";
	}

	struct FrameCaptureCameraState
	{
		std::string m_Name;
		// The reference view the capture request restored; empty otherwise.
		std::string m_ReferenceViewId;
		std::array<float, 3> m_Position{};
		std::array<float, 3> m_Forward{};
		std::array<float, 3> m_Up{};
		float m_VerticalFovDegrees = 0.0f;
		float m_NearPlane = 0.0f;
		float m_FarPlane = 0.0f;
	};

	// Everything a capture records about the frame that produced it. Frame-state
	// fields are sampled when the capture is issued, immediately before the
	// frame that records it is built.
	struct FrameCaptureMetadata
	{
		uint64_t m_RequestId = 0;
		std::string m_Label;
		std::string m_Note;
		FrameCaptureSource m_Source = FrameCaptureSource::Scene;
		FrameCaptureTiming m_Timing = FrameCaptureTiming::NextFrame;
		uint32_t m_SettleFrames = 0;
		uint32_t m_SettledFrames = 0;
		std::string m_Backend;
		std::string m_DemoId;
		std::string m_LabId;
		uint64_t m_FrameSerial = 0;
		uint64_t m_FrameIndex = 0;
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;
		std::string m_DisplayFormat;
		std::string m_ImageFile;
		FrameCaptureCameraState m_Camera{};
		std::optional<double> m_FixedDeltaTime;
		double m_TotalTime = 0.0;
		bool m_DevelopmentTools = false;
		FrameCaptureReadiness m_Readiness{};
		std::string m_CapturedAtUtc;
	};

	// Serializes the metadata as schema-versioned UTF-8 JSON.
	[[nodiscard]] std::string SerializeFrameCaptureMetadata(
		const FrameCaptureMetadata& metadata) noexcept;
}
