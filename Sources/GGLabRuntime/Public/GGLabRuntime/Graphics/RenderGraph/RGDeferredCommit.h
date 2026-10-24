#pragma once
#include "GGLabFoundation/Base/CoreMacros.h"
#include "GGLabRuntime/Graphics/RHI/RHIFence.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace gglab
{
	struct RGExecuteContext;

	struct RGCommitContext
	{
		uint64_t m_FrameSerial = 0;
		RHIFencePoint m_Fence{};
	};

	// Checked at compile time: the callable is nothrow to construct, destroy and invoke,
	// and its result is ignored, so a commit cannot report failure.
	// Caller obligations, not checked: captures hold stable CPU service pointers and
	// values, never GPU resource owners, and a commit does not start GPU work, wait or
	// retain any enqueue capability.
	class RGDeferredCommit final
	{
	public:
		template <typename Function>
			requires (!std::is_same_v<std::remove_cvref_t<Function>, RGDeferredCommit> &&
				std::is_nothrow_constructible_v<std::decay_t<Function>, Function&&> &&
				std::is_nothrow_destructible_v<std::decay_t<Function>> &&
				std::is_nothrow_invocable_r_v<void, std::decay_t<Function>&, const RGCommitContext&>)
		RGDeferredCommit(Function&& function) noexcept :
			m_Callable(std::make_unique<Callable<std::decay_t<Function>>>(std::forward<Function>(function)))
		{
		}
		GGLAB_DELETE_COPYABLE(RGDeferredCommit);
		RGDeferredCommit(RGDeferredCommit&&) noexcept = default;
		RGDeferredCommit& operator=(RGDeferredCommit&&) noexcept = default;
		~RGDeferredCommit() noexcept = default;

	private:
		class CallableBase
		{
		public:
			virtual ~CallableBase() = default;
			virtual void Invoke(const RGCommitContext& context) noexcept = 0;
		};

		template <typename Function> class Callable final : public CallableBase
		{
		public:
			template <typename Value>
			explicit Callable(Value&& function) noexcept : m_Function(std::forward<Value>(function)) {}
			void Invoke(const RGCommitContext& context) noexcept override { std::invoke(m_Function, context); }
		private:
			Function m_Function;
		};

		std::unique_ptr<CallableBase> m_Callable;
		friend class RGDeferredCommitList;
	};

	// One owner-thread submission. The context's owner must commit with its real fence or
	// discard. The list is independent of Renderer outcomes; moves transfer inactive ownership.
	class RGDeferredCommitList final
	{
	public:
		RGDeferredCommitList() noexcept = default;
		GGLAB_DELETE_COPYABLE(RGDeferredCommitList);
		RGDeferredCommitList(RGDeferredCommitList&&) noexcept = default;
		RGDeferredCommitList& operator=(RGDeferredCommitList&&) noexcept = default;
		~RGDeferredCommitList() noexcept;

		void Seal() noexcept;
		// A valid fence is required even for an empty list. A list that has already failed,
		// for example through an enqueue after Seal, discards its batch. Otherwise the
		// original batch runs once, in order, and an enqueue rejected while it runs does not
		// stop it.
		void CommitAll(const RGCommitContext& context) noexcept;
		void Discard() noexcept;
		[[nodiscard]] bool HasFailed() const noexcept { return m_Failed; }

	private:
		enum class State : uint8_t
		{
			Open,
			Sealed,
			Settling,
			Settled,
		};

		void Enqueue(RGDeferredCommit commit) noexcept;
		void Fail(const char* reason) noexcept;

		std::vector<RGDeferredCommit> m_Commits;
		State m_State = State::Open;
		bool m_Failed = false;
		friend struct RGExecuteContext;
	};
}
