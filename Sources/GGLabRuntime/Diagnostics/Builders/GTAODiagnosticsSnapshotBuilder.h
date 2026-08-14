#pragma once

namespace gglab
{
	class Renderer;
	class RenderGraph;
	struct GTAODiagnosticsSnapshot;
	struct GTAOSettings;

	[[nodiscard]] GTAODiagnosticsSnapshot BuildGTAODiagnosticsSnapshot(
		const Renderer& renderer, const RenderGraph& renderGraph,
		const GTAOSettings* authoringSettings, const GTAOSettings* requestedSettings,
		bool overrideActive) noexcept;
}
