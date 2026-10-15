#pragma once
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	class RenderGraph;
	enum class NonFiniteWatchSemantic : uint8_t
	{
		MotionVectors,
		AmbientOcclusion,
		TemporalInput,
		PostProcessInput,
		Count,
	};

	inline constexpr size_t NonFiniteWatchSemanticCount = static_cast<size_t>(NonFiniteWatchSemantic::Count);
	[[nodiscard]] std::string_view GetNonFiniteWatchSemanticName(NonFiniteWatchSemantic semantic) noexcept;
	struct NonFiniteWatchDefault
	{
		NonFiniteWatchSemantic m_Semantic{};
		RGResourceAddress m_Address;
		NonFiniteClaimResult m_Claim;
		std::string m_Failure;
		// Ready means a live, exact version was resolved; the scan has not completed.
		bool m_Ready = false;
		std::optional<RGResourceHandle> m_Resource;
	};
	using NonFiniteWatchDefaults = std::array<NonFiniteWatchDefault, NonFiniteWatchSemanticCount>;

	// Frame-local blackboard metadata. Pipelines declare applicability independently
	// of pass bindings, so an omitted active binding cannot silently become inactive.
	class NonFiniteWatchDefaultRegistry final
	{
	public:
		void Declare(std::array<bool, NonFiniteWatchSemanticCount> applicable) noexcept;
		// Multiple consumer cuts must refer to the same version. Resolution selects
		// the earliest live cut without changing original graph liveness.
		void Register(NonFiniteWatchSemantic semantic, RGResourceHandle resource, RGResourceAddress address) noexcept;
		[[nodiscard]] NonFiniteWatchDefaults Resolve(const RenderGraph& graph) const noexcept;

	private:
		struct Binding { RGResourceHandle m_Resource; RGResourceAddress m_Address; };
		struct Entry { bool m_Applicable = false; std::vector<Binding> m_Bindings; };
		bool m_Declared = false;
		bool m_ConflictingDeclaration = false;
		std::array<Entry, NonFiniteWatchSemanticCount> m_Entries;
	};
	[[nodiscard]] NonFiniteWatchDefaults ResolveNonFiniteWatchDefaults(const RenderGraph& graph) noexcept;
	[[nodiscard]] std::optional<uint32_t> GetNonFiniteDefaultWatchTier(
		const RenderGraph& graph, const RGObservationTarget& target) noexcept;
}
