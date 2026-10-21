#pragma once
#include "Lab/LabParameter.h"

#include <cstdint>
#include <optional>
#include <string>

namespace gglab
{
	enum class ParameterEffectState : uint8_t
	{
		Requested,
		Applied,
		Published,
		Consumed,
		Superseded,
		NoChange,
		EffectNotReached,
		Failed,
	};

	// A revision identifies the batch, not the state of an asynchronous domain.
	// Immediate state has no domain/generation; deferred and rebuilt state has both.
	struct ParameterStateIdentity
	{
		uint64_t m_Revision = 0;
		std::string m_Domain;
		uint64_t m_Generation = 0;

		bool operator==(const ParameterStateIdentity&) const noexcept = default;
	};

	struct ParameterEffectSnapshot
	{
		LabParameterId m_Parameter;
		ParameterEffectClass m_DeclaredClass = ParameterEffectClass::Immediate;
		ParameterEffectState m_State = ParameterEffectState::Requested;
		ParameterStateIdentity m_Identity;
		uint64_t m_Requested = 0;
		std::optional<uint64_t> m_Applied;
		std::optional<uint64_t> m_Published;
		std::optional<uint64_t> m_FirstConsumed;
		std::string m_Failure;

		[[nodiscard]] bool IsTerminal() const noexcept;
	};

	// Owner-held evidence for one parameter. This is neither a capture job nor a
	// scheduler. The owning domain reports application/publication; the frame owner
	// reports the exact identity used by a successfully submitted frame. An aborted
	// frame reports no consumption. Repeated evidence preserves the first stamps.
	class ParameterEffectReceipt final
	{
	public:
		[[nodiscard]] static std::optional<ParameterEffectReceipt> Begin(
			LabParameterId parameter, ParameterEffectClass declaredClass,
			uint64_t revision, uint64_t requestedFrame) noexcept;

		[[nodiscard]] const ParameterEffectSnapshot& GetSnapshot() const noexcept
		{
			return m_Snapshot;
		}
		[[nodiscard]] bool RecordApplied(ParameterEffectClass actualClass,
			const ParameterStateIdentity& identity, uint64_t frame) noexcept;
		[[nodiscard]] bool RecordPublished(
			const ParameterStateIdentity& identity, uint64_t frame) noexcept;
		[[nodiscard]] bool RecordSubmittedFrame(
			const ParameterStateIdentity& consumedIdentity, uint64_t frame) noexcept;
		[[nodiscard]] bool RecordNoChange(uint64_t frame) noexcept;
		[[nodiscard]] bool Supersede() noexcept;
		[[nodiscard]] bool Finish() noexcept;
		[[nodiscard]] bool Fail(std::string reason) noexcept;

	private:
		explicit ParameterEffectReceipt(ParameterEffectSnapshot snapshot) noexcept;
		ParameterEffectSnapshot m_Snapshot;
	};
}
