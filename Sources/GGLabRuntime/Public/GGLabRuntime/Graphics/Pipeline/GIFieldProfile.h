#pragma once
#include "GGLabRuntime/Graphics/Pipeline/GIFieldReadiness.h"

#include <array>
#include <cstdint>
#include <string>

namespace gglab
{
	// Host-declared base preset, not a runtime generation, cache key or qualification.
	// Lowercase dotted ASCII segments: letter, then letters/digits/underscore; at most
	// 128 bytes. The positive revision persists; effective overrides belong in the receipt.
	struct GIFieldProfileIdentity
	{
		std::string m_Id;
		uint32_t m_Revision = 0;
		bool operator==(const GIFieldProfileIdentity&) const = default;
	};

	struct GIFieldVolumeDescriptor
	{
		std::array<uint32_t, 3> m_Counts{ 33, 10, 39 };
		std::array<float, 3> m_Minimum{ -17.25f, -1.5f, -23.25f };
		float m_Spacing = 1;
		// Required for preparation; an empty default is not an inferred Coastal profile.
		GIFieldProfileIdentity m_Profile;
	};

	// Immutable CPU-only constructor inputs. No borrowed Host strings, scene/GPU owners
	// or inferred support certificate. Lighting/scene epochs retain their separate stamps.
	struct GIFieldProfileReceipt
	{
		static constexpr uint32_t SchemaVersion = 1;
		GIFieldVolumeDescriptor m_Volume;
		uint32_t m_IrradianceInteriorWidth = 0;
		uint32_t m_RaysPerProbe = 0;
		uint32_t m_Seed = 0;
		float m_MaximumDistance = 0;
		float m_Hysteresis = 0;
		float m_ViewBias = 0;
		bool m_EnableReceiverSampling = false;
		bool m_EnableFeedback = false;
		bool m_ProgressiveSampling = false;
		GIFieldReadinessSettings m_Readiness;
		bool m_ConstantEnvironment = false;
		std::array<float, 3> m_ConstantRadiance{};
	};
}
