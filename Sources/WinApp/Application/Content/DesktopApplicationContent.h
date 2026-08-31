#pragma once
#include "ApplicationContentRegistration.h"

#include <string_view>

namespace gglab
{
	class LabRuntime;
	struct ShaderPreviewPublicationArtifact;
	struct ShaderPreviewRuntimeSessionSnapshot;

	inline constexpr std::string_view DesktopStartDemoId = "Demo.Start";
	inline constexpr std::string_view DesktopPlaygroundDemoId = "Demo.Playground";
	inline constexpr std::string_view DesktopLabHostDemoId = "Demo.LabHost";
	inline constexpr std::string_view DesktopDefaultLabId = "gglab.lab.culling";
	inline constexpr std::string_view DesktopShaderGraphPreviewLabId =
		"gglab.lab.shader_graph_preview";

	[[nodiscard]] ApplicationContentRegistration CreateDesktopApplicationContent() noexcept;
	void SynchronizeDesktopShaderPreviewLab(LabRuntime& runtime,
		const ShaderPreviewPublicationArtifact& publication,
		const ShaderPreviewRuntimeSessionSnapshot& snapshot) noexcept;
}
