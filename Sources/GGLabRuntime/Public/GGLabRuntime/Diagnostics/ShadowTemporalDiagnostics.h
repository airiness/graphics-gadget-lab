#pragma once

#include "GGLabRuntime/Diagnostics/Snapshots/ShadowDiagnosticsSnapshot.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	struct ShadowTemporalSample
	{
		uint64_t m_FrameSerial = 0;
		// Center translation uses the current frame's texel scale.
		std::array<Vector2, MaxDirectionalShadowCascades> m_ProjectionDeltaTexels{};
		std::array<Vector2, MaxDirectionalShadowCascades> m_WorldUnitsPerTexel{};
		// Current minus previous scale, in world units per texel.
		std::array<Vector2, MaxDirectionalShadowCascades> m_TexelScaleDelta{};
		// Signed residual of the resolved center from the nearest integer texel grid.
		std::array<Vector2, MaxDirectionalShadowCascades> m_GridErrorTexels{};
		uint32_t m_CascadeCount = 0;
		bool m_HasComparison = false;
	};

	// Owns a bounded, CPU-only history. A sample compares only adjacent completed
	// frame plans with the same camera, light orientation and projection policy.
	class ShadowTemporalDiagnostics
	{
	public:
		static constexpr size_t MaxSamples = 128;

		void Reset() noexcept;
		// Returns true when this is a new frame. A reset frame records zero delta.
		[[nodiscard]] bool Record(const ShadowDiagnosticsSnapshot& snapshot,
			uint64_t cameraId, std::string_view referenceId) noexcept;
		[[nodiscard]] const std::vector<ShadowTemporalSample>& GetSamples() const noexcept
		{
			return m_Samples;
		}
		[[nodiscard]] uint32_t GetResetCount() const noexcept { return m_ResetCount; }

	private:
		[[nodiscard]] bool IsComparable(const ShadowDiagnosticsSnapshot& current,
			uint64_t cameraId, std::string_view referenceId) const noexcept;

		ShadowDiagnosticsSnapshot m_Previous{};
		std::vector<ShadowTemporalSample> m_Samples;
		std::string m_ReferenceId;
		uint64_t m_CameraId = 0;
		uint32_t m_ResetCount = 0;
		bool m_HasPrevious = false;
	};
}
