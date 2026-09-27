#pragma once
#include "GGLabRuntime/Core/Math/Vector.h"
#include "GGLabRuntime/Graphics/WorldSun.h"
#include "GGLabRuntime/Graphics/RHI/RHIBuffer.h"
#include "GGLabRuntime/Graphics/RHI/RHITexture.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace gglab
{
	// Authoring uses meters, inverse meters, and linear Rec.709 scattering coefficients.
	// These are RGB transport coefficients, not a spectral atmosphere conversion.
	struct AtmosphereSettings
	{
		float m_PlanetRadiusMeters = 6360000.0f;
		float m_HeightMeters = 100000.0f;
		Vector3 m_PlanetCenterMeters = Vector3(0.0f, -6360000.0f, 0.0f);
		float m_WorldUnitsToMeters = 1.0f;
		Vector3 m_RayleighScattering = Vector3(5.802e-6f, 13.558e-6f, 33.1e-6f);
		float m_RayleighScaleHeightMeters = 8000.0f;
		float m_MieScattering = 3.996e-6f;
		float m_MieExtinction = 4.44e-6f;
		float m_MieScaleHeightMeters = 1200.0f;
		float m_MieAnisotropy = 0.8f;
		Vector3 m_Absorption = Vector3(0.65e-6f, 1.881e-6f, 0.085e-6f);
		float m_AbsorptionCenterMeters = 25000.0f;
		float m_AbsorptionHalfWidthMeters = 15000.0f;
		Vector3 m_GroundAlbedo = Vector3(0.3f, 0.3f, 0.3f);
	};

	// Explicit float4 rows match AtmosphereParameters in Atmosphere.hlsli; all lengths are km.
	struct AtmosphereGPU
	{
		Vector4 m_Radii{}; // bottom, top, unused, unused
		Vector4 m_Rayleigh{}; // coefficient / km, scale height km
		Vector4 m_Mie{}; // scattering / km, extinction / km, scale height km, g
		Vector4 m_Absorption{}; // coefficient / km, center km
		Vector4 m_Ground{}; // albedo, absorption half width km
		Vector4 m_Sun{}; // normalized RGB illuminance, angular radius
		Vector4 m_Observer{}; // radius km, sun zenith cosine, unused, unused
	};
	static_assert(sizeof(AtmosphereGPU) == 112);
	static_assert(offsetof(AtmosphereGPU, m_Radii) == 0);
	static_assert(offsetof(AtmosphereGPU, m_Rayleigh) == 16);
	static_assert(offsetof(AtmosphereGPU, m_Mie) == 32);
	static_assert(offsetof(AtmosphereGPU, m_Absorption) == 48);
	static_assert(offsetof(AtmosphereGPU, m_Ground) == 64);
	static_assert(offsetof(AtmosphereGPU, m_Sun) == 80);
	static_assert(offsetof(AtmosphereGPU, m_Observer) == 96);

	[[nodiscard]] AtmosphereGPU ResolveAtmosphere(const AtmosphereSettings& settings,
		const ResolvedWorldSun& sun, const Vector3& cameraWorldPosition) noexcept;
	// Dependency masks: bit 0 transmittance, bit 1 multiple scattering, bit 2 sky view.
	[[nodiscard]] uint32_t AtmosphereDirtyMask(const AtmosphereGPU& previous, const AtmosphereGPU& current) noexcept;
	[[nodiscard]] Vector3 EvaluateAtmosphereTransmittance(const AtmosphereGPU& atmosphere,
		float altitudeMeters, float zenithCosine, float distanceMeters) noexcept;

	inline constexpr std::array<uint32_t, 3> AtmosphereLutWidths{ 256, 32, 192 };
	inline constexpr std::array<uint32_t, 3> AtmosphereLutHeights{ 64, 32, 108 };
	inline constexpr std::array<const char*, 3> AtmosphereLutNames{
		"Atmosphere.Transmittance", "Atmosphere.MultipleScattering", "Atmosphere.SkyView" };

	struct AtmosphereDiagnostics
	{
		std::array<uint64_t, 3> m_Generations{};
		uint32_t m_DirtyMask = 0;
		bool m_Available = false;
		AtmosphereGPU m_Parameters{};
	};

	class RenderAtmosphereAccess
	{
	public:
		virtual ~RenderAtmosphereAccess() = default;
		virtual bool Begin(const AtmosphereGPU& parameters, const std::array<uint64_t, 3>& shaderGenerations) noexcept = 0;
		virtual void Disable() noexcept = 0;
		virtual RHITextureHandle GetTexture(uint32_t index) const noexcept = 0;
		virtual RHITextureDesc GetTextureDesc(uint32_t index) const noexcept = 0;
		virtual bool IsInitialized(uint32_t index) const noexcept = 0;
		virtual RHIBufferHandle GetConstants() const noexcept = 0;
		virtual void NotifyExecuted(uint32_t index) noexcept = 0;
		virtual AtmosphereDiagnostics GetDiagnostics() const noexcept = 0;
	};
}
