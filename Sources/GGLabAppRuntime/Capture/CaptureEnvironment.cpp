#include "Capture/CaptureEnvironment.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace gglab
{
	namespace
	{
		using Json = nlohmann::ordered_json;

		Json Text(const std::string& value)
		{
			return value.empty() ? Json(nullptr) : Json(value);
		}

		template <typename T>
		Json Optional(const std::optional<T>& value)
		{
			return value ? Json(*value) : Json(nullptr);
		}

		const char* GetBackendName(RHIBackendType backend) noexcept
		{
			switch (backend)
			{
			case RHIBackendType::DX12:
				return "dx12";
			case RHIBackendType::Vulkan:
				return "vulkan";
			case RHIBackendType::Unknown:
				break;
			}
			return "unknown";
		}

		Json Device(const RHIDeviceIdentity& device)
		{
			if (!device.m_Available)
			{
				return nullptr;
			}
			Json api = Json::object();
			if (device.m_FeatureLevel)
			{
				api["featureLevel"] = *device.m_FeatureLevel;
			}
			if (device.m_ShaderModel)
			{
				api["shaderModel"] = *device.m_ShaderModel;
			}
			if (device.m_ApiVersion)
			{
				api["apiVersion"] = *device.m_ApiVersion;
			}
			return Json{
				{ "backend", GetBackendName(device.m_Backend) },
				{ "adapterName", device.m_AdapterName },
				{ "vendorId", device.m_VendorId },
				{ "deviceId", device.m_DeviceId },
				{ "subsystemId", Optional(device.m_SubsystemId) },
				{ "revision", Optional(device.m_Revision) },
				{ "driverVersion", Optional(device.m_DriverVersion) },
				{ "driverVersionRaw", Text(device.m_DriverVersionRaw) },
				{ "api", std::move(api) },
				{ "dedicatedVideoMemoryBytes", Optional(device.m_DedicatedVideoMemoryBytes) },
				{ "compatibilityIdentity", device.m_CompatibilityIdentity } };
		}

		Json Timestamp(std::chrono::system_clock::time_point time)
		{
			if (time == std::chrono::system_clock::time_point{})
			{
				return nullptr;
			}
			return std::format("{:%Y-%m-%dT%H:%M:%SZ}", std::chrono::floor<std::chrono::seconds>(time));
		}
	}

	std::string SerializeCaptureEnvironment(
		const CaptureEnvironment& environment, std::string_view runIdentity) noexcept try
	{
		const CaptureBuildDeclaration& build = environment.m_Host.m_Build;
		const RHIDeviceIdentity& device = environment.m_Device;
		const auto validation = [&device](const std::optional<RHIValidationLevel>& level)
			{
				return device.m_Available && level ? Json(GetRHIValidationLevelName(*level)) : Json(nullptr);
			};
		const Json record{
			{ "schemaVersion", CaptureEnvironmentSchemaVersion },
			{ "build", {
				{ "source", build.m_Available ? "build-stamp" : "unavailable" },
				{ "revision", build.m_Available ? Text(build.m_Revision) : Json(nullptr) },
				{ "dirty", build.m_Available ? Optional(build.m_Dirty) : Json(nullptr) },
				{ "configuration", Text(build.m_Configuration) },
				{ "compiler", Text(build.m_Compiler) },
				{ "pixIntegration", device.m_Available ? Json(device.m_PixIntegration) : Json(nullptr) } } },
			{ "process", {
				{ "runIdentity", runIdentity.empty() ? Json(nullptr) : Json(std::string(runIdentity)) },
				{ "startedAtUtc", Timestamp(environment.m_StartedAt) },
				{ "os", Text(environment.m_Host.m_OperatingSystem) },
				{ "host", Text(environment.m_Host.m_Presentation) } } },
			{ "device", Device(device) },
			{ "validation", {
				{ "requested", validation(device.m_RequestedValidation) },
				{ "effective", validation(device.m_EffectiveValidation) } } } };
		return record.dump();
	}
	catch (...)
	{
		return {};
	}
}
