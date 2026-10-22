#pragma once
#include "GGLabRuntime/Graphics/WorldLighting.h"

#include <cstdint>
#include <memory>
#include <string>

namespace gglab
{
	enum class WorldLightingMutationOutcome
	{
		Rejected,
		Unchanged,
		Requested,
	};

	struct WorldLightingMutationResult
	{
		WorldLightingMutationOutcome m_Outcome = WorldLightingMutationOutcome::Rejected;
		uint64_t m_PreviousGeneration = 0;
		uint64_t m_Generation = 0;
	};

	// Prepared only for one uninterrupted render-thread safe boundary. Dropping a
	// plan has no effect. Commit cannot reject: no callbacks or World/control edits
	// may intervene between preparation and commit.
	class PreparedWorldLightingMutationBase
	{
	public:
		virtual ~PreparedWorldLightingMutationBase() = default;
		[[nodiscard]] virtual bool HasChanges() const noexcept = 0;
		[[nodiscard]] virtual WorldLightingMutationResult Commit() noexcept = 0;
	};

	// Render-thread authoring control, used only before frame planning/build. It
	// accepts domain inputs, never content profile IDs, hashes or exposure advice.
	class WorldLightingControlBase
	{
	public:
		virtual ~WorldLightingControlBase() = default;
		virtual void SynchronizeWorldLighting(World& world, uint64_t sessionIdentity) noexcept = 0;
		// Read-only preparation. Settle existing authoring with Synchronize first,
		// outside this batch's attribution boundary, never inside its commit.
		[[nodiscard]] virtual std::unique_ptr<PreparedWorldLightingMutationBase> PrepareWorldLightingInputs(
			World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs,
			std::string& error) noexcept = 0;
		// Standalone convenience only; a multi-source batch must use prepared plans.
		[[nodiscard]] virtual WorldLightingMutationResult ApplyWorldLightingInputs(
			World& world, uint64_t sessionIdentity, const WorldLightingInputs& inputs) noexcept = 0;
		[[nodiscard]] virtual WorldLightingPublication GetWorldLightingPublication() const noexcept = 0;
	};
}
