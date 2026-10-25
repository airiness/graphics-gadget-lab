#include "RGObjectArenaSelfTests.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGBlackboard.h"
#include "GGLabRuntime/Graphics/RenderGraph/RGObjectArena.h"

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace gglab
{
	struct RGObjectArenaTestAccess
	{
		static void FailAfter(RGObjectArena& arena, size_t allocations) noexcept
		{
			arena.m_AllocationsUntilFailure = allocations;
		}
		static void* Allocate(RGObjectArena& arena, size_t bytes, size_t alignment) noexcept
		{
			return arena.AllocateBytes(bytes, alignment);
		}
	};

	namespace
	{
		struct Probe
		{
			Probe(uint32_t id, std::vector<uint32_t>& order) : m_Id(id), m_Order(order) {}
			~Probe() noexcept { m_Order.push_back(m_Id); }
			GGLAB_DELETE_COPYABLE_MOVABLE(Probe);
			Probe* m_Self = this;
			uint32_t m_Id;
			std::vector<uint32_t>& m_Order;
			std::string m_Text = std::string(200, 'a');
			std::vector<uint32_t> m_Values{ 1, 2, 3, 4 };
			std::unique_ptr<uint32_t> m_Owner = std::make_unique<uint32_t>(42);
			std::array<std::byte, 256> m_Padding{};
		};

		struct Nested
		{
			Nested(RGObjectArena& arena, std::vector<uint32_t>& order) : m_Order(order)
			{
				m_Child = arena.Make<Probe>(1, order);
			}
			~Nested() noexcept
			{
				// The earlier-completed object and its dedicated/regular blocks remain alive.
				m_Order.push_back(m_Child->m_Self == m_Child && *m_Child->m_Owner == 42 ? 2 : 99);
			}
			Probe* m_Child = nullptr;
			std::vector<uint32_t>& m_Order;
		};

		template <size_t Alignment> struct alignas(Alignment) Aligned
		{
			std::byte m_Value{};
		};

		bool RunFailureChild(const char* name, const char* reason) noexcept
		{
			const auto result = RunSelfTestChild("--rg-object-arena-child", name);
			const auto& output = result.m_Output;
			const bool passed = result.m_Completed && result.m_ExitCode != 0 &&
				output.find("RGObjectArena contract failure:") != std::string::npos &&
				output.find(reason) != std::string::npos &&
				output.find("unexpected construction") == std::string::npos;
			if (!passed)
			{
				std::fprintf(stderr, "Arena %s: exit=%u completed=%u\n%s", name,
					result.m_ExitCode, result.m_Completed ? 1u : 0u, output.c_str());
			}
			return passed;
		}
	}

	int RunRGObjectArenaChild(std::string_view scenario) noexcept
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		constexpr size_t maximum = std::numeric_limits<size_t>::max();
		if (scenario == "zero-block" || scenario == "unaligned-block" || scenario == "overflow-block")
		{
			const size_t bytes = scenario == "zero-block" ?
				0 :
				(scenario == "unaligned-block" ? 31 : maximum & ~(alignof(std::max_align_t) - 1));
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = bytes });
		}
		else
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 32 });
			if (scenario == "zero-size")
			{
				RGObjectArenaTestAccess::Allocate(arena, 0, 1);
			}
			else if (scenario == "bad-alignment")
			{
				RGObjectArenaTestAccess::Allocate(arena, 1, 3);
			}
			else if (scenario == "overflow-size")
			{
				RGObjectArenaTestAccess::Allocate(arena, maximum, 16);
			}
			else if (scenario == "oom" || scenario == "record-oom")
			{
				struct RecordProbe
				{
					RecordProbe() noexcept { std::fputs("unexpected construction\n", stderr); }
					~RecordProbe() noexcept {}
					std::array<std::byte, 32> m_Data{};
				};
				RGObjectArenaTestAccess::FailAfter(arena, scenario == "oom" ? 0 : 1);
				arena.Make<RecordProbe>();
			}
			else if (scenario == "destructor-allocation")
			{
				struct Reenter
				{
					RGObjectArena* m_Arena;
					~Reenter() noexcept { m_Arena->Make<uint32_t>(); }
				};
				arena.Make<Reenter>(&arena);
			}
			else
			{
				return 2;
			}
		}
		return 0;
	}

	void RunRGObjectArenaSelfTests(SelfTestContext& context) noexcept
	{
		static_assert(!std::is_copy_constructible_v<RGObjectArena>);
		static_assert(!std::is_move_constructible_v<RGObjectArena>);
		static_assert(!std::is_copy_assignable_v<RGObjectArena>);
		static_assert(!std::is_move_assignable_v<RGObjectArena>);
		std::vector<uint32_t> order;
		order.reserve(5000);
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 512 });
			const auto empty = arena.GetStatistics();
			context.Check(empty.m_ReservedPayloadBytes == 0 && empty.m_BlockOverheadBytes == 0 &&
				empty.m_ObjectCount == 0, "An empty object arena allocates no blocks");
			std::vector<Probe*> probes;
			for (uint32_t index = 0; index < 4000; ++index)
			{
				probes.push_back(arena.Make<Probe>(index, order));
			}
			bool stable = true;
			for (uint32_t index = 0; index < probes.size(); ++index)
			{
				const auto* probe = probes[index];
				stable = stable && probe->m_Self == probe && probe->m_Id == index &&
					probe->m_Text == std::string(200, 'a') && probe->m_Values[3] == 4 &&
					*probe->m_Owner == 42;
			}
			const auto stats = arena.GetStatistics();
			context.Check(stable && stats.m_ObjectBytes > (1u << 20) && order.empty(),
				"Nonmovable objects, self pointers and owning members survive growth beyond 1 MiB");
			context.Check(stats.m_ObjectCount == 4000 && stats.m_DestructorRecordCount == 4000 &&
				stats.m_UsedPayloadBytes >= stats.m_ObjectBytes + stats.m_DestructorRecordBytes &&
				stats.m_ReservedPayloadBytes >= stats.m_UsedPayloadBytes &&
				stats.m_RegularBlockCount > 1 && stats.m_BlockOverheadBytes > 0,
				"Arena counters separate completed objects, records, used payload and block overhead");
		}
		bool reverse = order.size() == 4000;
		for (uint32_t index = 0; index < order.size(); ++index)
		{
			reverse = reverse && order[index] == 3999 - index;
		}
		context.Check(reverse, "Every nontrivial arena object is destroyed once in reverse order");
		order.clear();
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			arena.Make<Nested>(arena, order);
		}
		context.Check(order == std::vector<uint32_t>{ 2, 1 },
			"Nested construction destroys by completed order and keeps earlier payload blocks alive");
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			arena.Make<uint32_t>(7);
			const auto before = arena.GetStatistics();
			const auto* a = arena.Make<Aligned<64>>();
			const auto* b = arena.Make<Aligned<256>>();
			const auto* c = arena.Make<Aligned<4096>>();
			arena.Make<std::array<std::byte, 1024>>();
			arena.Make<uint32_t>(8);
			const auto stats = arena.GetStatistics();
			context.Check(reinterpret_cast<uintptr_t>(a) % 64 == 0 &&
				reinterpret_cast<uintptr_t>(b) % 256 == 0 && reinterpret_cast<uintptr_t>(c) % 4096 == 0 &&
				stats.m_DedicatedBlockCount == 4 && stats.m_RegularBlockCount == before.m_RegularBlockCount &&
				stats.m_DestructorRecordCount == 0,
				"Large and over-aligned trivial objects use dedicated blocks without replacing regular storage");
		}
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			const auto* first = arena.Make<std::array<std::byte, 64>>();
			arena.Make<std::byte>();
			const auto stats = arena.GetStatistics();
			context.Check(first && stats.m_RegularBlockCount == 2 && stats.m_UsedPayloadBytes == 65 &&
				stats.m_ReservedPayloadBytes == 128, "Exact regular capacity rolls over without wasting a full block");
		}
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			arena.Make<std::byte>();
			arena.Make<uint64_t>(9);
			const auto stats = arena.GetStatistics();
			context.Check(stats.m_ObjectBytes == 9 && stats.m_UsedPayloadBytes == 16 &&
				stats.m_ReservedPayloadBytes == 64 && stats.m_DestructorRecordBytes == 0,
				"Consumed padding contributes to used payload without inflating object bytes");
		}
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			arena.Make<std::array<std::byte, 63>>();
			const auto* aligned = arena.Make<uint64_t>(9);
			context.Check(reinterpret_cast<uintptr_t>(aligned) % alignof(uint64_t) == 0 &&
				arena.GetStatistics().m_RegularBlockCount == 2 && *aligned == 9,
				"Alignment padding is checked against remaining capacity before pointer formation");
		}
		order.clear();
		{
			RGObjectArena arena({ .m_RegularBlockPayloadBytes = 64 });
			RGBlackboard blackboard(arena);
			auto& original = blackboard.Create<Probe>(1, order);
			blackboard.ClearIndex();
			auto& replacement = blackboard.Create<Probe>(2, order);
			context.Check(original.m_Self == &original && &replacement != &original &&
				&blackboard.Get<Probe>() == &replacement && order.empty(),
				"Clearing the blackboard index preserves previous payload references until arena destruction");
		}
		context.Check(order == std::vector<uint32_t>{ 2, 1 }, "Cleared blackboard payloads retain automatic destruction");
		for (const char* name : { "zero-block", "unaligned-block" })
		{
			context.Check(RunFailureChild(name, "invalid regular block payload size"), name);
		}
		for (const char* name : { "overflow-block", "overflow-size" })
		{
			context.Check(RunFailureChild(name, "size overflow"), name);
		}
		context.Check(RunFailureChild("zero-size", "invalid object size"), "Zero raw allocation fails");
		context.Check(RunFailureChild("bad-alignment", "invalid alignment"), "Invalid alignment fails");
		context.Check(RunFailureChild("oom", "allocation failed"), "Allocation failure precedes object construction");
		context.Check(RunFailureChild("record-oom", "allocation failed"), "Record reservation failure precedes construction");
		context.Check(RunFailureChild("destructor-allocation", "allocation during destruction"),
			"Destructor allocation fails in every configuration");
	}
}
