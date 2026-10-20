#pragma once

#include <cstdint>
#include <memory>
#include <utility>

namespace gglab
{
	// Accounting is independent of Runtime and RHI lifetime. Admission is serialized
	// by the capture owner; a last CPU consumer may return credit on any thread.
	class CapturePayloadBudget
	{
		struct Ledger;

	public:
		class Lease
		{
		public:
			Lease() noexcept = default;
			~Lease();
			Lease(const Lease&) = delete;
			Lease& operator=(const Lease&) = delete;
			Lease(Lease&& other) noexcept;
			Lease& operator=(Lease&& other) noexcept;

			void Reset() noexcept;
			// Retire GPU bytes after copy/unmap, retaining the logical payload slots.
			void ReduceTo(uint64_t bytes) noexcept;
			[[nodiscard]] uint64_t GetBytes() const noexcept { return m_Bytes; }

		private:
			friend class CapturePayloadBudget;
			Lease(std::shared_ptr<Ledger> ledger, uint64_t bytes, uint32_t slots) noexcept;

			std::shared_ptr<Ledger> m_Ledger;
			uint64_t m_Bytes = 0;
			uint32_t m_Slots = 0;
		};

		CapturePayloadBudget();
		// Call only after aggregate admission, including producers not yet using leases.
		[[nodiscard]] Lease Reserve(uint64_t bytes, uint32_t slots) const noexcept;
		[[nodiscard]] std::pair<uint64_t, uint32_t> GetUsage() const noexcept;

	private:
		std::shared_ptr<Ledger> m_Ledger;
	};
}
