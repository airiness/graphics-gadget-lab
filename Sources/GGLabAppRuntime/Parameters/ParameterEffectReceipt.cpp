#include "Parameters/ParameterEffectReceipt.h"

#include <utility>

namespace gglab
{
	bool ParameterEffectSnapshot::IsTerminal() const noexcept
	{
		return m_State == ParameterEffectState::Consumed ||
			m_State == ParameterEffectState::Superseded ||
			m_State == ParameterEffectState::NoChange ||
			m_State == ParameterEffectState::EffectNotReached ||
			m_State == ParameterEffectState::Failed;
	}

	ParameterEffectReceipt::ParameterEffectReceipt(ParameterEffectSnapshot snapshot) noexcept :
		m_Snapshot(std::move(snapshot))
	{
	}

	std::optional<ParameterEffectReceipt> ParameterEffectReceipt::Begin(
		LabParameterId parameter, ParameterEffectClass declaredClass,
		uint64_t revision, uint64_t requestedFrame) noexcept
	{
		if (!parameter.IsValid() || revision == 0 || requestedFrame == 0)
		{
			return std::nullopt;
		}
		return ParameterEffectReceipt({
			.m_Parameter = std::move(parameter),
			.m_DeclaredClass = declaredClass,
			.m_Identity = { .m_Revision = revision },
			.m_Requested = requestedFrame,
		});
	}

	bool ParameterEffectReceipt::RecordApplied(ParameterEffectClass actualClass,
		const ParameterStateIdentity& identity, uint64_t frame) noexcept
	{
		if (m_Snapshot.IsTerminal() || frame < m_Snapshot.m_Requested ||
			identity.m_Revision != m_Snapshot.m_Identity.m_Revision)
		{
			return false;
		}
		const bool immediate = actualClass == ParameterEffectClass::Immediate;
		const bool hasDomain = !identity.m_Domain.empty() && identity.m_Generation != 0;
		const bool noDomain = identity.m_Domain.empty() && identity.m_Generation == 0;
		if ((immediate && !noDomain) || (!immediate && !hasDomain))
		{
			return false;
		}
		if (actualClass != m_Snapshot.m_DeclaredClass)
		{
			if (!m_Snapshot.m_Applied)
			{
				m_Snapshot.m_Identity = identity;
				m_Snapshot.m_Applied = frame;
				if (immediate)
				{
					m_Snapshot.m_Published = frame;
				}
			}
			(void)Fail("EffectMismatch");
			return false;
		}
		if (m_Snapshot.m_Applied)
		{
			return identity == m_Snapshot.m_Identity && frame >= *m_Snapshot.m_Applied;
		}
		m_Snapshot.m_Identity = identity;
		m_Snapshot.m_Applied = frame;
		m_Snapshot.m_State = ParameterEffectState::Applied;
		if (immediate)
		{
			m_Snapshot.m_Published = frame;
			m_Snapshot.m_State = ParameterEffectState::Published;
		}
		return true;
	}

	bool ParameterEffectReceipt::RecordPublished(
		const ParameterStateIdentity& identity, uint64_t frame) noexcept
	{
		if (m_Snapshot.IsTerminal() || !m_Snapshot.m_Applied ||
			identity != m_Snapshot.m_Identity || frame < *m_Snapshot.m_Applied)
		{
			return false;
		}
		if (m_Snapshot.m_Published)
		{
			return frame >= *m_Snapshot.m_Published;
		}
		m_Snapshot.m_Published = frame;
		m_Snapshot.m_State = ParameterEffectState::Published;
		return true;
	}

	bool ParameterEffectReceipt::RecordSubmittedFrame(
		const ParameterStateIdentity& consumedIdentity, uint64_t frame) noexcept
	{
		if (m_Snapshot.IsTerminal() || !m_Snapshot.m_Published ||
			consumedIdentity != m_Snapshot.m_Identity || frame < *m_Snapshot.m_Published)
		{
			return false;
		}
		m_Snapshot.m_FirstConsumed = frame;
		m_Snapshot.m_State = ParameterEffectState::Consumed;
		return true;
	}

	bool ParameterEffectReceipt::RecordNoChange(uint64_t frame) noexcept
	{
		if (m_Snapshot.m_State != ParameterEffectState::Requested ||
			frame < m_Snapshot.m_Requested)
		{
			return false;
		}
		m_Snapshot.m_Applied = frame;
		m_Snapshot.m_State = ParameterEffectState::NoChange;
		return true;
	}

	bool ParameterEffectReceipt::Supersede() noexcept
	{
		if (m_Snapshot.IsTerminal())
		{
			return false;
		}
		m_Snapshot.m_State = ParameterEffectState::Superseded;
		return true;
	}

	bool ParameterEffectReceipt::Finish() noexcept
	{
		if (m_Snapshot.IsTerminal())
		{
			return false;
		}
		m_Snapshot.m_State = ParameterEffectState::EffectNotReached;
		return true;
	}

	bool ParameterEffectReceipt::Fail(std::string reason) noexcept
	{
		if (m_Snapshot.IsTerminal() || reason.empty())
		{
			return false;
		}
		m_Snapshot.m_Failure = std::move(reason);
		m_Snapshot.m_State = ParameterEffectState::Failed;
		return true;
	}
}
