#pragma once

namespace gglab
{
	class Renderer;
	struct PersistentSceneBufferSnapshot;

	void BuildPersistentSceneBufferSnapshot(
		const Renderer& renderer,
		PersistentSceneBufferSnapshot& outSnapshot) noexcept;
}
