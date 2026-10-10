#pragma once
#include "Capture/FrameCaptureReadiness.h"
#include "GGLabRuntime/Graphics/Capture/FrameCaptureTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

	// One temporal consumer of the frame's plan and the services it contributed.
	struct FrameCaptureTemporalConsumer
	{
		std::string m_Name;
		bool m_Requested = false;
		std::string m_Status;
		std::string m_DisableReason;
		std::vector<std::string> m_Services;
	};

	// Resolved GTAO settings of the display view; temporal accumulation may still be
	// inactive (see the temporal-gtao consumer).
	struct FrameCaptureGTAOSettings
	{
		bool m_Enabled = false;
		float m_Radius = 0.0f;
		float m_FalloffStart = 0.0f;
		float m_FalloffEnd = 0.0f;
		uint32_t m_DirectionCount = 0;
		uint32_t m_StepCount = 0;
		uint32_t m_DenoiseRadius = 0;
		bool m_TemporalAccumulation = false;
		uint32_t m_TemporalMaxSamples = 0;
	};

	// Temporal state of the frame a capture recorded. Settings are the resolved
	// display-view settings; the resolve may still be disabled or unavailable.
	struct FrameCaptureTemporalState
	{
		// Temporal AA consumer state; m_Consumers records every consumer.
		bool m_Requested = false;
		std::string m_Status;
		std::string m_DisableReason;
		std::vector<FrameCaptureTemporalConsumer> m_Consumers;
		// Services the frame enabled: the union of the active consumers' services.
		std::vector<std::string> m_Services;
		uint64_t m_SessionIdentity = 0;
		uint64_t m_ResetIdentity = 0;
		uint32_t m_JitterIndex = 0;
		uint32_t m_JitterSequenceLength = 0;
		std::array<float, 2> m_JitterPixels{};
		float m_MaxHistoryFeedback = 0.0f;
		float m_DepthAbsoluteThreshold = 0.0f;
		float m_DepthRelativeThreshold = 0.0f;
		float m_VelocityWeightScale = 0.0f;
		float m_LuminanceWeightScale = 0.0f;
		float m_NeighborhoodClampExpansion = 0.0f;
		float m_HistoryRelaxation = 0.0f;
		std::string m_HistoryAccumulation;
		std::string m_HistoryRectification;
		float m_VarianceClipGamma = 0.0f;
		std::string m_HistoryFilter;
		std::string m_CurrentFilter;
		std::string m_MotionSelection;
		std::string m_PostTemporalView;
		std::string m_ResolutionPreset;
		float m_TextureLodBiasOffset = 0.0f;
		FrameCaptureGTAOSettings m_GTAO{};
		// LOD bias the frame's material textures used.
		float m_TextureLodBias = 0.0f;
		std::array<uint32_t, 2> m_RenderExtent{};
		std::array<uint32_t, 2> m_DisplayExtent{};
		// Render width over display width.
		float m_RenderScale = 1.0f;
	};

	// Camera-path sequence frame a capture belongs to.
	struct FrameCaptureSequenceInfo
	{
		uint64_t m_SequenceId = 0;
		std::string m_CameraPathId;
		uint32_t m_CameraPathVersion = 0;
		uint32_t m_Frame = 0;
		uint32_t m_FrameCount = 0;
		// Samples of a supersampled reference frame; zero for an ordinary sequence.
		uint32_t m_ReferenceSamples = 0;
		float m_ReferenceTextureLodBias = 0.0f;
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
		FrameCaptureTemporalState m_Temporal{};
		std::optional<FrameCaptureSequenceInfo> m_Sequence;
		// Diagnostic tap name of a Diagnostic capture; empty otherwise.
		std::string m_DiagnosticTap;
		std::string m_CapturedAtUtc;
	};

	// Serializes the metadata as schema-versioned UTF-8 JSON.
	[[nodiscard]] std::string SerializeFrameCaptureMetadata(
		const FrameCaptureMetadata& metadata) noexcept;
}
