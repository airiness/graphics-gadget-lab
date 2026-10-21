#pragma once
#include "GGLabRuntime/Graphics/RHI/RHITypes.h"

#include <cstdint>
#include <optional>
#include <string>

namespace gglab
{
	enum class RHIValidationLevel : uint8_t
	{
		Off,
		Standard,
		// Standard validation plus GPU-based validation.
		Gpu,
	};

	[[nodiscard]] constexpr const char* GetRHIValidationLevelName(RHIValidationLevel level) noexcept
	{
		switch (level)
		{
		case RHIValidationLevel::Off:
			return "off";
		case RHIValidationLevel::Standard:
			return "standard";
		case RHIValidationLevel::Gpu:
			return "gpu";
		}
		return "off";
	}

	// The adapter, driver and API level a context runs on, as its backend read them
	// at device creation. A value the backend cannot determine stays empty rather
	// than estimated.
	struct RHIDeviceIdentity
	{
		// False for contexts without a native adapter; every other field is then unset.
		bool m_Available = false;
		RHIBackendType m_Backend = RHIBackendType::Unknown;
		std::string m_AdapterName;
		uint32_t m_VendorId = 0;
		uint32_t m_DeviceId = 0;
		std::optional<uint32_t> m_SubsystemId;
		std::optional<uint32_t> m_Revision;
		// In the backend's or vendor's convention; empty when the encoding is unknown.
		std::optional<std::string> m_DriverVersion;
		// The backend's raw driver version value as hexadecimal.
		std::string m_DriverVersionRaw;
		// DX12 reports a feature level such as "12_2" and the highest shader model
		// such as "6_8"; Vulkan reports its API version such as "1.3.290".
		std::optional<std::string> m_FeatureLevel;
		std::optional<std::string> m_ShaderModel;
		std::optional<std::string> m_ApiVersion;
		std::optional<uint64_t> m_DedicatedVideoMemoryBytes;
		// The same value as RHIDevice::GetAdapterCompatibilityIdentity.
		std::string m_CompatibilityIdentity;
		RHIValidationLevel m_RequestedValidation = RHIValidationLevel::Off;
		// What the backend actually enabled; empty when it cannot tell.
		std::optional<RHIValidationLevel> m_EffectiveValidation;
		// Whether this build integrates the PIX GPU capturer.
		bool m_PixIntegration = false;
	};
}
