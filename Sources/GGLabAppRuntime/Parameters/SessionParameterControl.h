#pragma once
#include "Parameters/ParameterBatch.h"
#include "Parameters/ParameterSource.h"
#include "Parameters/ParameterFrameState.h"
#include "Parameters/HostViewParameters.h"
#include "ApplicationToolingIntegration.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"
#include "GGLabRuntime/Graphics/LightingProfile.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	inline constexpr size_t SessionParameterMaxItems = 128;
	inline constexpr size_t SessionParameterMaxBatches = 128;

	struct ParameterSourceAdapters;
	class DemoBase;
	class LabRuntime;
	class RenderEnvironmentAccess;
	class WorldLightingControlBase;
	struct DemoFrameFeedback;
	struct RenderView;
	struct FrameCaptureGate;

	// Sole transaction/receipt owner across Host and Lab sources. One queued application
	// is independent of bounded, owner-addressed records waiting for effects or collection.
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
		// Advances once for each committed or discarded change to Host view override
		// presence or value. NoChange and inherited value changes leave it unchanged.
		[[nodiscard]] uint64_t GetHostViewRevision() const noexcept { return m_HostViewRevision; }
		[[nodiscard]] std::span<const LabParameterValue> GetHostViewOverrides() const noexcept;
		// Host view values as the next frame resolves them, before frame-local patches.
		[[nodiscard]] std::vector<HostViewProjectionEntry> DescribeHostView() const;
		[[nodiscard]] const LabParameterSet& GetHostParameters() const noexcept;
		[[nodiscard]] const LabParameterSet* GetLabParameters() const noexcept;
		[[nodiscard]] std::span<const WorldLightingProfile> GetLightingProfiles() const noexcept;
		[[nodiscard]] const LightingProfilePublication& GetLightingProfilePublication() const noexcept;
		[[nodiscard]] const std::string& GetLightingProfileError() const noexcept;
		[[nodiscard]] bool SupportsParameterEffect(const LabParameterDesc& desc) const noexcept;
		// Descriptor validation without reservations, mutation or retained source borrows.
		[[nodiscard]] bool ValidateSequenceAssignments(std::span<LabParameterValue> values,
			const std::optional<std::vector<std::string>>& expectedEffects, std::string& error) const noexcept;
		// A live source proves coverage for this owner's applied, outstanding effects only.
		[[nodiscard]] bool CoversReadinessGate(ParameterBatchOwner owner,
			const FrameCaptureGate& gate) const noexcept;
		void FinishParameterBatch(uint64_t id, ParameterBatchOwner owner = {}) noexcept;
		void FinishParameterBatches(ParameterBatchOwner owner) noexcept;
		[[nodiscard]] uint64_t RequestParameterBatch(std::span<const LabParameterValue> values,
			std::string& error, ParameterBatchOwner owner = {}) noexcept;
		// Exact Host view override states under the same admission. The caller verifies the
		// expected run, which recorded stamps carry. Content and Host view revision are
		// checked at admission and again at the safe boundary; a mismatch applies nothing.
		[[nodiscard]] uint64_t RequestHostViewOverrides(const HostViewControlStamp& expected,
			std::span<const HostViewOverrideState> overrides, std::string& error,
			ParameterBatchOwner owner = {}) noexcept;
		// Internal owners may append after application; external admission retains one result slot.
		// Status borrows survive other records' admission/collection, until their own collection.
		// The compatibility view exposes only the external client's single retained result.
		[[nodiscard]] const ParameterBatchStatus* GetParameterBatchStatus() const noexcept;
		[[nodiscard]] const ParameterBatchStatus* GetParameterBatchStatus(
			uint64_t id, ParameterBatchOwner owner = {}) const noexcept;
		[[nodiscard]] std::vector<ParameterBatchStatus> GetParameterBatchStatuses(
			ParameterBatchOwner owner) const;
		[[nodiscard]] size_t GetParameterBatchCount() const noexcept { return m_ParameterBatches.size(); }
		[[nodiscard]] bool HasPendingParameterBatches() const noexcept;
		[[nodiscard]] std::optional<ParameterBatchStatus> TakeParameterBatchResult(
			uint64_t id, ParameterBatchOwner owner = {}) noexcept;
		void OnRuntimeFailed(std::string_view reason) noexcept;
		void Finish() noexcept;
		void OnFrameReady(uint64_t serial) noexcept;
		void ApplyViewSettings(ResolvedViewRenderSettings& settings, bool enablePreExposure) noexcept;
		void OnFrameBuilt(uint64_t serial, uint64_t lightingGeneration,
			const ResolvedViewRenderSettings& settings, const RenderView* view = nullptr) noexcept;
		void OnFrameSubmitted(const DemoFrameFeedback& feedback) noexcept;
		[[nodiscard]] ParameterFrameState CaptureFrameState(uint64_t serial,
			uint64_t requestedLightingGeneration, uint64_t activeLightingGeneration) const;
		void CompleteFrameState(ParameterFrameState& frame) const noexcept;

	private:
		void RefreshSources() noexcept;
		[[nodiscard]] bool ValidateParameterBatch(
			std::span<const LabParameterValue> values, std::string& error) const noexcept;
		[[nodiscard]] const LabParameter* Find(const LabParameterId& id) const noexcept;
		struct ParameterBatch;
		void RefreshParameterBatchStatus(ParameterBatch& batch) noexcept;
		void EndParameterBatch(ParameterBatch& batch, bool superseded, std::string reason) noexcept;
		void EndHostViewOperation(ParameterBatch& batch, std::string_view conflict,
			std::string_view reason) noexcept;
		void EndForContentChange(ParameterBatch& batch, std::string reason) noexcept;
		void ApplyParameterBatch(ParameterBatch& batch, uint64_t serial) noexcept;
		[[nodiscard]] bool IsAdmissionBlocked(ParameterBatchOwner owner) const noexcept;
		[[nodiscard]] uint64_t AdmitParameterBatch(std::span<const LabParameterValue> values,
			std::string& error, ParameterBatchOwner owner) noexcept;
		[[nodiscard]] uint64_t AdmitHostViewOverrides(const HostViewControlStamp& expected,
			std::span<const HostViewOverrideState> overrides, std::string& error,
			ParameterBatchOwner owner) noexcept;
		[[nodiscard]] HostViewControlStamp MakeHostViewStamp(std::string_view runIdentity) const;
		void SupersedeOverwrittenItems(const ParameterBatch& applied) noexcept;
		void AdvanceHostViewRevision() noexcept;
		struct ParameterBatch
		{
			ParameterBatchStatus m_Status;
			ParameterBatchOwner m_Owner;
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
			bool m_Rebuilding = false;
			bool m_WaitingForContent = false;
		};
		std::unique_ptr<ParameterSourceAdapters> m_Sources;
		DemoBase* m_Demo = nullptr;
		LabRuntime* m_Lab = nullptr;
		std::string m_ContentId;
		uint64_t m_ContentSerial = 0;
		uint64_t m_ObservedLabRevision = 0;
		// Heap records keep query borrows stable when another record is added/collected.
		std::vector<std::unique_ptr<ParameterBatch>> m_ParameterBatches;
		uint64_t m_QueuedParameterBatch = 0;
		uint64_t m_LastReadyFrameSerial = 0;
		bool m_MutatingParameterBatch = false;
		uint64_t m_NextParameterBatchId = 1;
		uint64_t m_ParameterRevision = 1;
		// Maintained at the Host override write sites, independent of lazy tracking.
		uint64_t m_HostViewRevision = 1;
		bool m_ParameterIntakeFailed = false;
		bool m_Enabled = false;
		bool m_Transitioning = false;
	};
}
