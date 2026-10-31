#pragma once
#include "Graphics/RenderFrameOutcome.h"

#include <cstdint>
#include <memory>

namespace gglab
{
	// CPU state only: neither deferred commits nor Renderer settlement own GPU objects.
	// In-place GPU writes cannot be rolled back after a failed/unknown submission.
	class GIFieldUpdateState final
	{
	public:
		bool Begin(uint64_t serial) noexcept
		{
			if (m_Invalid || serial == 0 || serial <= m_Serial || (m_Serial != 0 && !m_Settled))
			{
				return false;
			}
			m_Serial = serial;
			m_Recorded = false;
			m_Settled = false;
			m_Committed = false;
			return true;
		}
		void MarkRecorded(uint64_t serial) noexcept
		{
			if (serial == m_Serial && !m_Settled)
			{
				m_Recorded = true;
			}
		}
		void Settle(const RenderFrameOutcome& outcome) noexcept
		{
			if (outcome.m_FrameSerial != m_Serial || m_Settled)
			{
				return;
			}
			m_Settled = true;
			m_Submitted = outcome.m_Kind == RenderFrameOutcomeKind::Submitted && outcome.m_Fence.IsValid();
			m_Invalid = outcome.m_Kind == RenderFrameOutcomeKind::Failed ||
				outcome.m_Kind == RenderFrameOutcomeKind::CompletionUnknown ||
				(outcome.m_Kind == RenderFrameOutcomeKind::Submitted && !outcome.m_Fence.IsValid());
		}
		bool Commit(uint64_t serial) noexcept
		{
			if (serial != m_Serial || !m_Settled || !m_Submitted || !m_Recorded || m_Committed || m_Invalid)
			{
				return false;
			}
			m_Committed = true;
			return true;
		}
		[[nodiscard]] bool IsInvalid() const noexcept { return m_Invalid; }

	private:
		uint64_t m_Serial = 0;
		bool m_Recorded = false;
		bool m_Settled = false;
		bool m_Submitted = false;
		bool m_Committed = false;
		bool m_Invalid = false;
	};

	// A renderer frame has at most one field. This independent CPU lease survives a
	// safe pipeline replacement without prolonging its scene, assets or native resources.
	class GIFieldFrameState final
	{
	public:
		void BeginFrame(uint64_t serial) noexcept
		{
			m_Serial = serial;
			m_Field.reset();
		}
		bool Track(const std::shared_ptr<GIFieldUpdateState>& field, uint64_t serial) noexcept
		{
			if (!field || serial != m_Serial || m_Field || !field->Begin(serial))
			{
				return false;
			}
			m_Field = field;
			return true;
		}
		void Settle(const RenderFrameOutcome& outcome) noexcept
		{
			if (m_Field && outcome.m_FrameSerial == m_Serial)
			{
				m_Field->Settle(outcome);
				m_Field.reset();
			}
		}

	private:
		uint64_t m_Serial = 0;
		std::shared_ptr<GIFieldUpdateState> m_Field;
	};
}
