#pragma once
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterSource.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <functional>
#include <memory>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	inline constexpr size_t SessionParameterMaxItems = 128;

	struct ParameterSourceAdapters;
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
		~SessionParameterControl();
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
		[[nodiscard]] const LabParameterSet& GetHostParameters() const noexcept;
		[[nodiscard]] const LabParameterSet* GetLabParameters() const noexcept;
		[[nodiscard]] std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept;
		[[nodiscard]] const LightingProfilePublication& GetLightingProfilePublication() const noexcept;
		[[nodiscard]] const std::string& GetLightingProfileError() const noexcept;
		[[nodiscard]] bool SupportsParameterEffect(const LabParameterDesc& desc) const noexcept;
		// Descriptor validation without reservations, mutation or retained source borrows.
		[[nodiscard]] bool ValidateImmediateAssignments(
			std::span<LabParameterValue> values, std::string& error) const noexcept;
		void FinishParameterBatch(uint64_t id) noexcept;
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
		void RefreshSources() noexcept;
		[[nodiscard]] bool ValidateParameterBatch(
			std::span<const LabParameterValue> values, std::string& error) const noexcept;
		[[nodiscard]] const LabParameter* Find(const LabParameterId& id) const noexcept;
		void RefreshParameterBatchStatus() noexcept;
		void EndParameterBatch(bool superseded, std::string reason) noexcept;
		struct ParameterBatch
		{
			ParameterBatchStatus m_Status;
			std::vector<ParameterEffectReceipt> m_Receipts;
			std::vector<ParameterSourceBase*> m_Bindings;
			std::vector<bool> m_NoChange;
			struct ConsumedState
			{
				size_t m_Item = 0;
				ParameterStateIdentity m_Identity;
			};
			std::vector<ConsumedState> m_Consumed;
			uint64_t m_Session = 0;
			uint64_t m_FrameSerial = 0;
			ParameterDomainState m_Domain;
		};
		std::unique_ptr<ParameterSourceAdapters> m_Sources;
		DemoBase* m_Demo = nullptr;
		LabRuntime* m_Lab = nullptr;
		std::string m_ContentId;
		uint64_t m_ContentSerial = 0;
		uint64_t m_ObservedLabRevision = 0;
		std::optional<ParameterBatch> m_ParameterBatch;
		uint64_t m_NextParameterBatchId = 1;
		uint64_t m_ParameterRevision = 1;
		bool m_ParameterIntakeFailed = false;
		bool m_Enabled = false;
		bool m_Transitioning = false;
	};
}
