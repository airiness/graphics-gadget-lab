#pragma once
#include "GGLabRuntime/Graphics/Capture/NonFiniteWatchTypes.h"
#include "GGLabRuntime/Graphics/RHI/RHIHandles.h"

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
		Environment,
		Irradiance,
		Specular,
		AtmosphereTransmittance,
		AtmosphereMultipleScattering,
		AtmosphereSkyView,
		Count,
	};

	inline constexpr size_t NonFiniteWatchSemanticCount = static_cast<size_t>(NonFiniteWatchSemantic::Count);
	[[nodiscard]] constexpr uint32_t GetNonFiniteWatchTier(NonFiniteWatchSemantic semantic) noexcept
	{
		return semantic < NonFiniteWatchSemantic::Environment ? 1u : 2u;
	}
	// Ephemeral content identity, not a persisted artifact/cache identity. A pooled
	// texture can keep its handle while a producer publishes a different revision.
	struct NonFiniteWatchGeneration
	{
		RHITextureHandle m_Texture;
		uint64_t m_Revision = 0;
		[[nodiscard]] bool IsValid() const noexcept { return m_Texture.IsValid() && m_Revision != 0; }
		bool operator==(const NonFiniteWatchGeneration&) const noexcept = default;
	};
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
		NonFiniteWatchGeneration m_Generation;
		// The producer wrote this revision in the current frame. A later write can
		// replace it before rotated scans reach it, so it is scanned in this frame.
		bool m_WrittenThisFrame = false;
		std::vector<RGResourceAddress> m_Mips;
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
		void RegisterGeneration(NonFiniteWatchSemantic semantic, RGResourceHandle resource,
			RGResourceAddress address, NonFiniteWatchGeneration generation, bool writtenThisFrame = false) noexcept;
		[[nodiscard]] NonFiniteWatchDefaults Resolve(const RenderGraph& graph) const noexcept;

	private:
		struct Binding
		{
			RGResourceHandle m_Resource;
			RGResourceAddress m_Address;
			NonFiniteWatchGeneration m_Generation;
			bool m_WrittenThisFrame = false;
		};
		struct Entry
		{
			bool m_Applicable = false;
			std::vector<Binding> m_Bindings;
		};
		bool m_Declared = false;
		bool m_ConflictingDeclaration = false;
		std::array<Entry, NonFiniteWatchSemanticCount> m_Entries;
	};
	[[nodiscard]] NonFiniteWatchDefaults ResolveNonFiniteWatchDefaults(const RenderGraph& graph) noexcept;
	[[nodiscard]] std::optional<uint32_t> GetNonFiniteDefaultWatchTier(
		const RenderGraph& graph, const RGObservationTarget& target) noexcept;
}
