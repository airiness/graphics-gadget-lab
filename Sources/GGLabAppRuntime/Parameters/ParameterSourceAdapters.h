#pragma once
#include "Parameters/ParameterSource.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Parameters/HostViewParameters.h"
#include "Parameters/LightingParameterDomain.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <array>
#include <functional>
#include <optional>
#include <string_view>

namespace gglab
{
	class DemoBase;
	class LabRuntime;
	class WorldLightingControlBase;

	class HostViewParameterSource final : public ParameterSourceBase
	{
	public:
		using ViewResolver = std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)>;
		explicit HostViewParameterSource(ViewResolver resolver) noexcept;
		void Bind(DemoBase* demo) noexcept;
		void Refresh() noexcept;
		[[nodiscard]] bool HasDisplayView() const noexcept;
		[[nodiscard]] bool IsHost() const noexcept override { return true; }
		[[nodiscard]] const LabParameterSet* GetParameters() const noexcept override { return &m_Parameters; }
		[[nodiscard]] bool Supports(const LabParameterDesc&) const noexcept override { return true; }
		[[nodiscard]] std::unique_ptr<PreparedParameterSourceBase> Prepare(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept override;
		// Exact states: a present entry stays pinned even when it equals its inherited value,
		// and an absent entry follows the inheritance resolved now. Targets receive each
		// entry's resulting value in request order.
		[[nodiscard]] std::unique_ptr<PreparedParameterSourceBase> PrepareOverrides(
			std::span<const HostViewOverrideState> states, std::vector<LabParameterValue>& targets,
			std::string& error) noexcept;
		[[nodiscard]] bool MatchesFrame(const LabParameterValue& value,
			const ParameterSourceFrame& frame) const noexcept override;
		void ApplyViewSettings(ResolvedViewRenderSettings& settings, bool enablePreExposure) const noexcept;
		[[nodiscard]] std::span<const LabParameterValue> GetOverrides() const noexcept { return m_Overrides; }
		// The display view as the next frame resolves it before frame-local patches.
		[[nodiscard]] std::optional<ApplicationViewSettingsResolution> Resolve() const noexcept;

	private:
		class Prepared;
		class PreparedOverrides;
		[[nodiscard]] std::optional<ApplicationViewSettingsResolution> ResolveInherited() const noexcept;
		ViewResolver m_Resolver;
		DemoBase* m_Demo = nullptr;
		LabParameterSet m_Parameters;
		std::vector<LabParameterValue> m_Overrides;
	};

	class LabParameterSource final : public ParameterSourceBase
	{
	public:
		LabParameterSource(WorldLightingControlBase* control, LightingParameterDomain& domain) noexcept :
			m_Control(control), m_Domain(domain) {}
		void Bind(LabRuntime* lab, uint64_t identity) noexcept
		{
			m_Lab = lab;
			m_Identity = identity;
		}
		[[nodiscard]] bool IsHost() const noexcept override { return false; }
		[[nodiscard]] const LabParameterSet* GetParameters() const noexcept override;
		[[nodiscard]] bool Supports(const LabParameterDesc& desc) const noexcept override;
		[[nodiscard]] std::string_view GetDeferredEffectDomain() const noexcept override { return "lighting"; }
		[[nodiscard]] bool ContinuesContentChange(uint64_t batch,
			uint64_t previous, uint64_t current) const noexcept override;
		[[nodiscard]] ParameterPublicationObservation ObserveLifetime(
			const ParameterStateIdentity& identity) const noexcept override;
		[[nodiscard]] bool CoversReadinessGate(const ParameterStateIdentity& identity,
			std::string_view gate) const noexcept override
		{
			return m_Domain.CoversReadinessGate(identity, gate);
		}
		[[nodiscard]] bool Reserve(uint64_t id, std::span<const LabParameterValue> values,
			std::string& error) noexcept override;
		void Cancel(uint64_t id) noexcept override;
		[[nodiscard]] std::unique_ptr<PreparedParameterSourceBase> Prepare(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept override;
		[[nodiscard]] ParameterPublicationObservation ObservePublication(
			const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept override;
		[[nodiscard]] bool MatchesFrame(const LabParameterValue& value,
			const ParameterSourceFrame& frame) const noexcept override;

	private:
		class Prepared;
		WorldLightingControlBase* m_Control = nullptr;
		LightingParameterDomain& m_Domain;
		LabRuntime* m_Lab = nullptr;
		uint64_t m_Identity = 0;
	};

	class LightingProfileParameterSource final : public ParameterSourceBase
	{
	public:
		LightingProfileParameterSource(WorldLightingControlBase* control, LightingParameterDomain& domain) noexcept :
			m_Control(control), m_Domain(domain) {}
		void Bind(DemoBase* demo, uint64_t identity) noexcept;
		void Synchronize() noexcept;
		void Refresh(bool available) noexcept;
		void ObservePublication(uint64_t generation) noexcept;
		[[nodiscard]] bool IsHost() const noexcept override { return true; }
		[[nodiscard]] const LabParameterSet* GetParameters() const noexcept override { return &m_Parameters; }
		[[nodiscard]] bool Supports(const LabParameterDesc&) const noexcept override { return true; }
		[[nodiscard]] std::string_view GetDeferredEffectDomain() const noexcept override { return "lighting"; }
		[[nodiscard]] bool CoversReadinessGate(const ParameterStateIdentity& identity,
			std::string_view gate) const noexcept override
		{
			return m_Domain.CoversReadinessGate(identity, gate);
		}
		[[nodiscard]] bool Validate(const LabParameterValue& value, std::string& error) const noexcept override;
		[[nodiscard]] bool ValidateMissing(const LabParameterId& id, std::string& error) const noexcept;
		[[nodiscard]] std::unique_ptr<PreparedParameterSourceBase> Prepare(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept override;
		[[nodiscard]] ParameterPublicationObservation ObservePublication(
			const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept override
		{
			return m_Domain.ObservePublication(identity, frame);
		}
		[[nodiscard]] bool MatchesFrame(const LabParameterValue& value,
			const ParameterSourceFrame& frame) const noexcept override;
		[[nodiscard]] std::span<const WorldLightingProfile> GetProfiles() const noexcept { return m_Profiles; }
		[[nodiscard]] const LightingProfilePublication& GetPublication() const noexcept { return m_Publication; }
		[[nodiscard]] const std::string& GetError() const noexcept { return m_Error; }

	private:
		class Prepared;
		[[nodiscard]] int32_t FindCurrent() const noexcept;
		WorldLightingControlBase* m_Control = nullptr;
		LightingParameterDomain& m_Domain;
		DemoBase* m_Demo = nullptr;
		uint64_t m_Identity = 0;
		LabParameterSet m_Parameters;
		std::vector<WorldLightingProfile> m_Profiles;
		std::vector<WorldLightingState> m_PreparedProfiles;
		std::vector<Sha256Digest> m_Hashes;
		std::optional<LabParameterDesc> m_Descriptor;
		LightingProfilePublication m_Publication;
		std::string m_Error;
		bool m_Initialized = false;
	};

	// Source composition and read-only discovery, with no batch/receipt/result slot.
	struct ParameterSourceAdapters
	{
		ParameterSourceAdapters(RenderEnvironmentAccess& environment, WorldLightingControlBase* worldLighting,
			HostViewParameterSource::ViewResolver resolver) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(ParameterSourceAdapters);
		void Bind(DemoBase* demo, LabRuntime* lab, uint64_t identity, bool continueView = false) noexcept;
		void Refresh(bool available) noexcept;
		void PublishHostParameters() noexcept;
		[[nodiscard]] ParameterSourceBase* Find(const LabParameterId& id) noexcept;
		[[nodiscard]] bool ValidateMissing(const LabParameterId& id, std::string& error) noexcept;
		[[nodiscard]] std::array<ParameterSourceBase*, 3> Ordered() noexcept;

		LightingParameterDomain m_Domain;
		HostViewParameterSource m_View;
		LabParameterSource m_Lab;
		LightingProfileParameterSource m_Profiles;
		LabParameterSet m_HostParameters;
	};
}
