#pragma once
#include "GGLabRuntime/Graphics/RHI/RHIDeviceIdentity.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gglab
{
	inline constexpr uint32_t CaptureEnvironmentSchemaVersion = 1;

	// The build's declaration of the tracked source state it was built from. It is not
	// a binary identity: untracked files, build options and toolchain changes can
	// alter the executable without changing it.
	struct CaptureBuildDeclaration
	{
		// False when the build could not read its source state; the revision and
		// dirty state are then unknown.
		bool m_Available = false;
		// 40 lowercase hexadecimal digits when available.
		std::string m_Revision;
		std::optional<bool> m_Dirty;
		std::string m_Configuration;
		std::string m_Compiler;
	};

	// Facts the host supplies that stay constant for the process.
	struct CaptureHostEnvironment
	{
		CaptureBuildDeclaration m_Build;
		// Operating system name and version as the host reports them; empty when unknown.
		std::string m_OperatingSystem;
		// How the host presents frames, for example "windowed" or "hidden"; empty when
		// the host makes no such distinction.
		std::string m_Presentation;
	};

	// Process-constant provenance, published in session status and in every bundle
	// manifest. State that can change while the process runs, such as GPU profiling or
	// DevTools visibility, belongs to frame or measurement metadata instead.
	struct CaptureEnvironment
	{
		CaptureHostEnvironment m_Host;
		RHIDeviceIdentity m_Device;
		std::chrono::system_clock::time_point m_StartedAt{};
	};

	// The environment record as a JSON object, qualified by this process's run identity.
	[[nodiscard]] std::string SerializeCaptureEnvironment(
		const CaptureEnvironment& environment, std::string_view runIdentity) noexcept;
}
