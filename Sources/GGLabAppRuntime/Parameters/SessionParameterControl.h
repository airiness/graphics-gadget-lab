#pragma once
#include "Parameters/ParameterBatch.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <functional>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	inline constexpr size_t SessionParameterMaxItems = 128;

	class DemoBase;
	class LabRuntime;
	class RenderEnvironmentAccess;
	class WorldLightingControlBase;
	struct DemoFrameFeedback;
	struct RenderView;

	// One session-owned transaction/result across host and Lab sources. LabRuntime
	// retains the safe command and mutation boundary; it holds no second receipt.
	class SessionParameterControl final
	{
	public:
		explicit SessionParameterControl(RenderEnvironmentAccess& environment,
			WorldLightingControlBase* worldLighting = nullptr,
			std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)> viewResolver = {}) noexcept;
		GGLAB_DELETE_COPYABLE_MOVABLE(SessionParameterControl);
		// Lifecycle owner releases a binding before destroying its Demo/LabRuntime.
		// The identity is shared with frame planning, never recomputed by this source.
		void SynchronizeContent(DemoBase* demo, uint64_t contentIdentity,
			std::string_view demoId, bool transitioning) noexcept;
		void Enable() noexcept;
		[[nodiscard]] bool IsEnabled() const noexcept { return m_Enabled; }
		[[nodiscard]] bool IsAvailable() const noexcept;
		[[nodiscard]] const std::string& GetContentId() const noexcept { return m_ContentId; }
		[[nodiscard]] uint64_t GetContentIdentity() const noexcept { return m_ContentSerial; }
		[[nodiscard]] uint64_t GetParameterRevision() const noexcept { return m_ParameterRevision; }
		[[nodiscard]] const LabParameterSet& GetHostParameters() const noexcept { return m_HostParameters; }
		[[nodiscard]] const LabParameterSet* GetLabParameters() const noexcept;
		[[nodiscard]] std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept { return m_LightingProfiles; }
		[[nodiscard]] const LightingProfilePublication& GetLightingProfilePublication() const noexcept { return m_LightingProfilesState; }
		[[nodiscard]] const std::string& GetLightingProfileError() const noexcept { return m_ProfileRegistrationError; }
		[[nodiscard]] bool SupportsParameterEffect(const LabParameterDesc& desc) const noexcept;
		[[nodiscard]] uint64_t RequestParameterBatch(
			std::span<const LabParameterValue> values, std::string& error) noexcept;
		[[nodiscard]] const ParameterBatchStatus* GetParameterBatchStatus() const noexcept;
		[[nodiscard]] std::optional<ParameterBatchStatus> TakeParameterBatchResult(uint64_t id) noexcept;
		void OnRuntimeFailed(std::string_view reason) noexcept;
		void Finish() noexcept;
		void OnFrameReady(uint64_t serial) noexcept;
		void ApplyViewSettings(ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept;
		void OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration,
			const ResolvedViewRenderSettings& settings, const RenderView* view = nullptr) noexcept;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept;

	private:
		[[nodiscard]] std::optional<ApplicationViewSettingsResolution> ResolveInheritedViewSettings() const noexcept;
		void RefreshSources() noexcept;
		void RefreshLightingProfiles(LabParameterSet& host) noexcept;
		[[nodiscard]] bool IsLightingProfileParameter(const LabParameterId& id) const noexcept;
		[[nodiscard]] int32_t FindCurrentLightingProfile() const noexcept;
		void UpdateLightingProfilePublication(uint64_t lightingGeneration) noexcept;
		[[nodiscard]] const LabParameter* Find(const LabParameterId& id) const noexcept;
		void RefreshParameterBatchStatus() noexcept;
		void EndParameterBatch(bool superseded, std::string reason) noexcept;
		struct ParameterBatch
		{
			ParameterBatchStatus m_Status;
			std::vector<ParameterEffectReceipt> m_Receipts;
			std::vector<LabParameterValue> m_LabValues;
			std::vector<bool> m_NoChange;
			struct ConsumedState
			{
				size_t m_Item = 0;
				ParameterStateIdentity m_Identity;
			};
			std::vector<ConsumedState> m_Consumed;
			uint64_t m_Session = 0;
			uint64_t m_FrameSerial = 0;
			uint64_t m_FrameBakeRequestGeneration = 0;
			uint64_t m_ApplyBakeGeneration = 0;
			bool m_AttributedLightingRequest = false;
		};
		std::function<std::optional<ApplicationViewSettingsResolution>(DemoBase&)> m_ViewResolver;
		RenderEnvironmentAccess& m_Environment;
		WorldLightingControlBase* m_WorldLighting = nullptr;
		DemoBase* m_Demo = nullptr;
		LabRuntime* m_Lab = nullptr;
		std::string m_ContentId;
		uint64_t m_ContentSerial = 0;
		uint64_t m_ObservedLabRevision = 0;
		LabParameterSet m_HostParameters;
		std::vector<LabParameterValue> m_HostOverrides;
		std::vector<WorldLightingProfile> m_LightingProfiles;
		std::vector<WorldLightingState> m_PreparedLightingProfiles;
		std::vector<Sha256Digest> m_LightingProfileHashes;
		bool m_ProfilesInitialized = false;
		std::optional<LabParameterDesc> m_ProfileDescriptor;
		LightingProfilePublication m_LightingProfilesState;
		std::string m_ProfileRegistrationError;
		std::optional<ParameterBatch> m_ParameterBatch;
		uint64_t m_NextParameterBatchId = 1;
		uint64_t m_ParameterRevision = 1;
		bool m_ParameterIntakeFailed = false;
		bool m_Enabled = false;
		bool m_Transitioning = false;
	};
}
