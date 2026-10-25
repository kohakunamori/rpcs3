// A *synthetic* structural proxy for the PPU interpreter's additional
// instruction-type lookup. This does not execute RPCS3 or emulate instructions.
// The mask under test is generated directly from PPUInterpreter.h in CI.
#include "mask_generated.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

namespace
{
	constexpr u32 entries = 0x20000;
	std::array<std::uint8_t, entries> type_lookup{};
	std::array<std::uint64_t, entries> handler_lookup{};
	volatile std::uint64_t result_sink = 0;

	constexpr u32 decoder_index(u32 op)
	{
		return ((op >> 26) | (op << 6)) & (entries - 1);
	}

	NOINLINE std::uint64_t reference(u32 op)
	{
		const u32 index = decoder_index(op);
		switch (type_lookup[index])
		{
		case 1:
			if (!(op & 0x001f0000u))
				return handler_lookup[index] ^ 0x5555u;
			break;
		case 2:
			if (op & 0x00000800u)
				return handler_lookup[index] ^ 0x7777u;
			break;
		default:
			break;
		}
		return handler_lookup[index];
	}

	NOINLINE std::uint64_t candidate(u32 op)
	{
		if (!ppu_interpreter_detail::needs_special_decode(op))
			return handler_lookup[decoder_index(op)];

		return reference(op);
	}

	void initialize()
	{
		for (u32 index = 0; index < entries; ++index)
		{
			const u32 opcode = (index & 63u) << 26;
			// Only primary families guarded by the actual RPCS3 fast path
			// contain synthetic type-decoder special cases.
			type_lookup[index] = ppu_interpreter_detail::needs_special_decode(opcode)
				? static_cast<std::uint8_t>((index >> 6) % 3u) : 0;
			handler_lookup[index] = static_cast<std::uint64_t>(index) * 43u + 23u;
		}
	}

	void check_equivalence()
	{
		std::mt19937 rng(0x1999);
		for (u32 index = 0; index < entries; ++index)
		{
			const u32 base = ((index & 63u) << 26) | (index >> 6);
			for (int j = 0; j < 16; ++j)
			{
				const u32 op = base | (rng() & 0x03fff800u);
				if (reference(op) != candidate(op))
				{
					std::cerr << "Synthetic equivalence failed: 0x" << std::hex << op << '\n';
					std::exit(1);
				}
			}
		}
	}

	std::vector<u32> make_workload(bool common_mix)
	{
		std::mt19937 rng(common_mix ? 0x4140 : 0x4141);
		std::vector<u32> ops(1u << 18);
		constexpr std::array<u32, 16> common =
		{
			0x38600001u, 0x3c600000u, 0x60000000u, 0x64000000u,
			0x7c632214u, 0x7c0802a6u, 0x4e800020u, 0x48000000u,
			0x41820010u, 0x80630000u, 0x90630000u, 0x88630000u,
			0x98630000u, 0xa0630000u, 0xb0630000u, 0x1000002cu,
		};

		for (u32& op : ops)
			op = common_mix ? common[rng() % common.size()] : rng();

		return ops;
	}

	template <typename Func>
	double measure(const std::vector<u32>& ops, Func func, std::uint64_t& checksum)
	{
		const auto start = std::chrono::steady_clock::now();
		std::uint64_t value = 0;
		for (int repetition = 0; repetition < 20; ++repetition)
		{
			for (const u32 op : ops)
				value += func(op);
		}
		const auto end = std::chrono::steady_clock::now();
		checksum = value;
		result_sink = value;
		return std::chrono::duration<double, std::milli>(end - start).count();
	}

	bool benchmark(const char* profile, bool common_mix, std::ofstream& raw)
	{
		const auto ops = make_workload(common_mix);
		constexpr std::size_t pairs = 9;
		std::array<double, pairs> reference_times{};
		std::array<double, pairs> candidate_times{};

		std::uint64_t checksum0 = 0;
		std::uint64_t checksum1 = 0;
		measure(ops, reference, checksum0);
		measure(ops, candidate, checksum1);

		for (std::size_t i = 0; i < pairs; ++i)
		{
			if (i % 2 == 0)
			{
				reference_times[i] = measure(ops, reference, checksum0);
				candidate_times[i] = measure(ops, candidate, checksum1);
			}
			else
			{
				candidate_times[i] = measure(ops, candidate, checksum1);
				reference_times[i] = measure(ops, reference, checksum0);
			}

			if (checksum0 != checksum1)
			{
				std::cerr << "Checksum mismatch in " << profile << " pair " << i << '\n';
				return false;
			}

			raw << profile << ",reference," << i << ',' << reference_times[i] << ',' << checksum0 << '\n';
			raw << profile << ",candidate," << i << ',' << candidate_times[i] << ',' << checksum1 << '\n';
		}

		auto sorted_ref = reference_times;
		auto sorted_candidate = candidate_times;
		std::sort(sorted_ref.begin(), sorted_ref.end());
		std::sort(sorted_candidate.begin(), sorted_candidate.end());

		const double ref_median = sorted_ref[pairs / 2];
		const double candidate_median = sorted_candidate[pairs / 2];
		std::cout << std::fixed << std::setprecision(3)
			<< profile << ": original=" << ref_median
			<< "ms candidate=" << candidate_median
			<< "ms change=" << 100.0 * (candidate_median / ref_median - 1.0)
			<< "% (synthetic proxy, NOT RPCS3 FPS)\n";
		return true;
	}
}

int main()
{
	initialize();
	check_equivalence();

	std::ofstream report("ppu_proxy_windows_samples.csv");
	if (!report)
		return 2;

	report << "profile,variant,pair,elapsed_ms,checksum\n";
	if (!benchmark("common_opcodes", true, report))
		return 1;
	if (!benchmark("uniform_random", false, report))
		return 1;

	std::cout << "PASS: synthetic equivalence and paired MSVC lookup benchmarks\n";
	return 0;
}
