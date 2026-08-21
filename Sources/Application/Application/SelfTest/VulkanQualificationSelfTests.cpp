#include "Application/SelfTest/VulkanQualificationSelfTests.h"

#include "Application/Rendering/VulkanQualification.h"

namespace gglab
{
	void RunVulkanQualificationSelfTests(SelfTestContext& context) noexcept
	{
		VulkanQualificationOptions options{};
		context.Check(!options.IsConfigurationValid(),
			"Vulkan qualification rejects incomplete host configuration");
		context.Check(!options.HasRequiredRuntimePaths(),
			"Vulkan qualification reports missing host-supplied runtime paths");
		context.Check(!options.HasRequiredSurfaceFactory(),
			"Vulkan qualification reports a missing surface factory");
		context.Check(!options.HasRequiredPlatformHost(),
			"Vulkan qualification reports a missing platform host");

		options.m_ListAdapters = true;
		context.Check(!options.IsConfigurationValid(),
			"Vulkan adapter inspection rejects a missing surface factory");
		options.m_SurfaceFactory = reinterpret_cast<const VulkanSurfaceFactoryBase*>(1);
		context.Check(options.IsConfigurationValid(),
			"Vulkan adapter inspection does not require an unused platform host");
		context.Check(options.HasRequiredSurfaceFactory(),
			"Vulkan qualification accepts the supplied surface factory");
		options.m_ListAdapters = false;
		context.Check(!options.IsConfigurationValid(),
			"Vulkan frame qualification requires the platform host independently");
		options.m_Host = reinterpret_cast<VulkanQualificationHostBase*>(1);
		context.Check(options.HasRequiredPlatformHost(),
			"Vulkan frame qualification accepts the supplied platform host");
		context.Check(!options.IsConfigurationValid(),
			"Vulkan frame qualification rejects missing host-supplied runtime paths");

		options.m_ShaderSourceRoot = "Shaders";
		context.Check(!options.IsConfigurationValid(),
			"Vulkan frame qualification requires the shader cache root independently");
		context.Check(!options.HasRequiredRuntimePaths(),
			"Vulkan qualification requires the shader cache root independently");
		options.m_ShaderCacheRoot = "ShaderCache";
		context.Check(options.IsConfigurationValid(),
			"Vulkan frame qualification accepts both required host-supplied runtime paths");
		context.Check(options.HasRequiredRuntimePaths(),
			"Vulkan qualification accepts both required host-supplied runtime paths");

		context.Check(PassesVulkanQualificationValidationGate(false, false, 1, 1),
			"validation-disabled qualification does not require a messenger");
		context.Check(!PassesVulkanQualificationValidationGate(true, false, 0, 0),
			"validation-requested qualification requires an active messenger");
		context.Check(PassesVulkanQualificationValidationGate(true, true, 0, 0),
			"validation-requested qualification accepts a clean messenger");
		context.Check(!PassesVulkanQualificationValidationGate(true, true, 1, 1),
			"validation warnings and errors fail qualification");
	}
}
