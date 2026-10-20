#include "Graphics/Capture/CapturePayloadBudget.h"
#include "GGLabFoundation/Base/CoreMacros.h"

#include <mutex>
#include <utility>

namespace gglab
{
	struct CapturePayloadBudget::Ledger
	{
		std::mutex m_Mutex;
		uint64_t m_Bytes = 0;
		uint32_t m_Slots = 0;
	};

	CapturePayloadBudget::CapturePayloadBudget() : m_Ledger(std::make_shared<Ledger>())
	{
	}

	CapturePayloadBudget::Lease CapturePayloadBudget::Reserve(
		uint64_t bytes, uint32_t slots) const noexcept
	{
		const std::lock_guard lock(m_Ledger->m_Mutex);
		m_Ledger->m_Bytes += bytes;
		m_Ledger->m_Slots += slots;
		return Lease(m_Ledger, bytes, slots);
	}

	std::pair<uint64_t, uint32_t> CapturePayloadBudget::GetUsage() const noexcept
	{
		const std::lock_guard lock(m_Ledger->m_Mutex);
		return { m_Ledger->m_Bytes, m_Ledger->m_Slots };
	}

	CapturePayloadBudget::Lease::Lease(
		std::shared_ptr<Ledger> ledger, uint64_t bytes, uint32_t slots) noexcept :
		m_Ledger(std::move(ledger)), m_Bytes(bytes), m_Slots(slots)
	{
	}

	CapturePayloadBudget::Lease::~Lease()
	{
		Reset();
	}

	CapturePayloadBudget::Lease::Lease(Lease&& other) noexcept :
		m_Ledger(std::move(other.m_Ledger)),
		m_Bytes(std::exchange(other.m_Bytes, 0)), m_Slots(std::exchange(other.m_Slots, 0))
	{
	}

	CapturePayloadBudget::Lease& CapturePayloadBudget::Lease::operator=(Lease&& other) noexcept
	{
		if (this != &other)
		{
			Reset();
			m_Ledger = std::move(other.m_Ledger);
			m_Bytes = std::exchange(other.m_Bytes, 0);
			m_Slots = std::exchange(other.m_Slots, 0);
		}
		return *this;
	}

	void CapturePayloadBudget::Lease::Reset() noexcept
	{
		if (!m_Ledger)
		{
			return;
		}
		{
			const std::lock_guard lock(m_Ledger->m_Mutex);
			GGLAB_ASSERT(m_Ledger->m_Bytes >= m_Bytes && m_Ledger->m_Slots >= m_Slots);
			m_Ledger->m_Bytes -= m_Bytes;
			m_Ledger->m_Slots -= m_Slots;
		}
		m_Ledger.reset();
		m_Bytes = 0;
		m_Slots = 0;
	}

	void CapturePayloadBudget::Lease::ReduceTo(uint64_t bytes) noexcept
	{
		GGLAB_ASSERT(m_Ledger && bytes <= m_Bytes);
		const std::lock_guard lock(m_Ledger->m_Mutex);
		m_Ledger->m_Bytes -= m_Bytes - bytes;
		m_Bytes = bytes;
	}
}
