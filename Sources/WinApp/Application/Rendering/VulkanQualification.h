#pragma once
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace gglab
{
	class VulkanSurfaceFactoryBase;

	[[nodiscard]] constexpr inline bool PassesVulkanQualificationValidationGate(
		bool validationRequested, bool debugMessengerActive,
		uint64_t validationErrorCount, uint64_t validationWarningCount) noexcept
	{
		return !validationRequested ||
			(debugMessengerActive && validationErrorCount == 0 && validationWarningCount == 0);
	}

	struct VulkanQualificationDrawableExtent
	{
		uint32_t m_Width = 0;
		uint32_t m_Height = 0;

		[[nodiscard]] bool IsEmpty() const noexcept
		{
			return m_Width == 0 || m_Height == 0;
		}
	};

	// Platform behavior required by the deterministic qualification harness.
	// The Application platform layer owns the native window and implements
	// these operations; the Vulkan backend only consumes drawable extents and
	// requested state changes.
	class VulkanQualificationHostBase
	{
	public:
		virtual ~VulkanQualificationHostBase() = default;

		[[nodiscard]] virtual bool QueryDrawableExtent(
			VulkanQualificationDrawableExtent& outExtent, std::string& outError) const noexcept = 0;
		[[nodiscard]] virtual bool ResizeDrawable(
			uint32_t width, uint32_t height, std::string& outError) noexcept = 0;
		[[nodiscard]] virtual bool SetMinimized(bool minimized, std::string& outError) noexcept = 0;
	};

	struct VulkanQualificationOptions
	{
		// Borrowed for the duration of RunVulkanQualification.
		const VulkanSurfaceFactoryBase* m_SurfaceFactory = nullptr;
		VulkanQualificationHostBase* m_Host = nullptr;
		bool m_IsHostAbiSupported = false;
		bool m_RequestValidation = false;
		bool m_ListAdapters = false;
		std::optional<std::string> m_AdapterSelector;
		std::filesystem::path m_ShaderSourceRoot;
		std::filesystem::path m_ShaderCacheRoot;

		[[nodiscard]] bool HasRequiredSurfaceFactory() const noexcept
		{
			return m_SurfaceFactory != nullptr;
		}

		[[nodiscard]] bool HasRequiredPlatformHost() const noexcept
		{
			return m_Host != nullptr;
		}

		[[nodiscard]] bool HasRequiredRuntimePaths() const noexcept
		{
			return !m_ShaderSourceRoot.empty() && !m_ShaderCacheRoot.empty();
		}

		[[nodiscard]] bool IsConfigurationValid() const noexcept
		{
			return HasRequiredSurfaceFactory() &&
				(m_ListAdapters || (HasRequiredPlatformHost() && HasRequiredRuntimePaths()));
		}
	};

	// Runs the deterministic Vulkan bootstrap, adapter inspection and
	// minimal-frame/resource qualification path without selecting or falling
	// back to another RHI.
	[[nodiscard]] int RunVulkanQualification(const VulkanQualificationOptions& options) noexcept;
}
