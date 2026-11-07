#pragma once
#include "Lab/LabParameter.h"
#include "GGLabFoundation/Base/CoreMacros.h"
#include "Parameters/ParameterEffectReceipt.h"
#include "GGLabRuntime/Graphics/ViewRenderSettings.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gglab
{
	struct RenderView;
	struct RenderScene;

	struct ParameterSourceFrame
	{
		const ResolvedViewRenderSettings& m_ViewSettings;
		const RenderView* m_View = nullptr;
		uint64_t m_LightingGeneration = 0;
		uint64_t m_ContentIdentity = 0;
		const RenderScene* m_Scene = nullptr;
		uint64_t m_FrameSerial = 0;
	};

	// The coordinator retains only the observation boundary, not domain policy.
	struct ParameterDomainState
	{
		uint64_t m_Baseline = 0;
		uint64_t m_Applied = 0;
		bool m_Attributed = false;
	};

	struct ParameterApplication
	{
		ParameterEffectClass m_ActualClass = ParameterEffectClass::Immediate;
		ParameterStateIdentity m_Identity;
		bool m_NoChange = false;
		std::string m_Failure;
	};

	enum class ParameterPublication
	{
		Pending,
		Published,
		Superseded,
		Failed,
	};

	struct ParameterPublicationObservation
	{
		ParameterPublication m_State = ParameterPublication::Pending;
		std::string m_Failure;
	};

	// An ephemeral source plan, never a receipt/result owner. Prepare is read-only;
	// every plan commits without callbacks before any plan starts its effects.
	class PreparedParameterSourceBase
	{
	public:
		virtual ~PreparedParameterSourceBase() = default;
		GGLAB_DELETE_COPYABLE_MOVABLE(PreparedParameterSourceBase);
		[[nodiscard]] bool HasChanges() const noexcept { return !m_Changed.empty(); }
		[[nodiscard]] bool IsChanged(const LabParameterId& id) const noexcept
		{
			return std::ranges::find(m_Changed, id) != m_Changed.end();
		}
		virtual void Commit() noexcept = 0;
		virtual void ApplyEffects() noexcept {}
		// Runs after every source's effects, so inherited Host settings can be rebased.
		virtual void FinalizeEffects() noexcept {}
		[[nodiscard]] virtual ParameterApplication DescribeApplication(
			const LabParameterId& id, ParameterDomainState& domain) const noexcept
		{
			GGLAB_UNUSED(domain);
			return { .m_NoChange = !IsChanged(id) };
		}

	protected:
		PreparedParameterSourceBase() = default;
		std::vector<LabParameterId> m_Changed;
	};

	class ParameterSourceBase
	{
	public:
		ParameterSourceBase() = default;
		virtual ~ParameterSourceBase() = default;
		// Admitted item bindings retain this source address until termination.
		GGLAB_DELETE_COPYABLE_MOVABLE(ParameterSourceBase);
		[[nodiscard]] virtual bool IsHost() const noexcept = 0;
		[[nodiscard]] virtual const LabParameterSet* GetParameters() const noexcept = 0;
		[[nodiscard]] virtual bool Supports(const LabParameterDesc& desc) const noexcept = 0;
		[[nodiscard]] virtual std::string_view GetDeferredEffectDomain() const noexcept { return {}; }
		[[nodiscard]] virtual bool ContinuesContentChange(uint64_t batch,
			uint64_t previous, uint64_t current) const noexcept
		{
			GGLAB_UNUSED(batch);
			GGLAB_UNUSED(previous);
			GGLAB_UNUSED(current);
			return false;
		}
		// Lifecycle failures must close receipts even when no RHI frame is available.
		[[nodiscard]] virtual ParameterPublicationObservation ObserveLifetime(
			const ParameterStateIdentity& identity) const noexcept
		{
			GGLAB_UNUSED(identity);
			return {};
		}
		[[nodiscard]] virtual bool CoversReadinessGate(const ParameterStateIdentity& identity,
			std::string_view gate) const noexcept
		{
			GGLAB_UNUSED(identity);
			GGLAB_UNUSED(gate);
			return false;
		}
		[[nodiscard]] virtual bool Validate(const LabParameterValue& value, std::string& error) const noexcept
		{
			GGLAB_UNUSED(value);
			GGLAB_UNUSED(error);
			return true;
		}
		[[nodiscard]] virtual bool Reserve(uint64_t id, std::span<const LabParameterValue> values,
			std::string& error) noexcept
		{
			GGLAB_UNUSED(id);
			GGLAB_UNUSED(values);
			GGLAB_UNUSED(error);
			return true;
		}
		virtual void Cancel(uint64_t id) noexcept { GGLAB_UNUSED(id); }
		[[nodiscard]] virtual std::unique_ptr<PreparedParameterSourceBase> Prepare(uint64_t id,
			std::span<const LabParameterValue> values, std::string& error) noexcept = 0;
		[[nodiscard]] virtual ParameterPublicationObservation ObservePublication(
			const ParameterStateIdentity& identity, const ParameterSourceFrame& frame) const noexcept
		{
			GGLAB_UNUSED(identity);
			GGLAB_UNUSED(frame);
			return {};
		}
		[[nodiscard]] virtual bool MatchesFrame(const LabParameterValue& value,
			const ParameterSourceFrame& frame) const noexcept = 0;
	};
}
