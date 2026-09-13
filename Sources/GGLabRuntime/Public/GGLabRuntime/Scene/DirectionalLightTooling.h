#pragma once

#include "GGLabRuntime/Core/Math/Color.h"
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/ShadowSettings.h"

#include <cstdint>
#include <optional>

namespace gglab
{
	struct DirectionalLightObservation
	{
		// Versioned identity scoped to the World borrowed for this tooling draw.
		uint32_t m_Id = 0;
		Vector3 m_Direction = -Vector3::UnitY;
		Color m_Color = Color::White;
		float m_Intensity = 1.0f;
		std::optional<DirectionalShadowSettings> m_ShadowSettings;
	};

	class DirectionalLightViewBase
	{
	public:
		virtual ~DirectionalLightViewBase() = default;
		[[nodiscard]] virtual std::optional<DirectionalLightObservation> GetLight() const noexcept = 0;
	};

	// Borrowed only during synchronous tooling draw. Commands update authoring
	// state for subsequent frame construction; never retain these interfaces or IDs.
	class DirectionalLightControlBase
	{
	public:
		virtual ~DirectionalLightControlBase() = default;
		virtual void SetDirection(uint32_t id, const Vector3& direction) noexcept = 0;
		virtual void SetRadiance(uint32_t id, const Color& color, float intensity) noexcept = 0;
		virtual void SetShadowSettings(uint32_t id,
			const std::optional<DirectionalShadowSettings>& settings) noexcept = 0;
	};
}
