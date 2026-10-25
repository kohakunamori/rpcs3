// A *synthetic* structural proxy for the PPU interpreter's additional
// instruction-type lookup. This does not execute RPCS3 or emulate instructions.
// The mask under test is generated directly from PPUInterpreter.h in CI.
#include "mask_generated.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
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


	// Branch-only filter: cheap common-family tests before the fallback.
	NOINLINE std::uint64_t candidate_branch(u32 op)
	{
		const u32 primary = op >> 26;
		if (primary != 0x04u && primary != 0x1fu &&
			(primary < 0x20u || (primary & 1u) || primary > 0x36u))
		{
			return handler_lookup[decoder_index(op)];
		}
		return reference(op);
	}

	// Switch filter: let MSVC choose an optimal lowering for fixed families.
	NOINLINE std::uint64_t candidate_switch(u32 op)
	{
		switch (op >> 26)
		{
		case 0x04: case 0x1f:
		case 0x20: case 0x22: case 0x24: case 0x26:
		case 0x28: case 0x2a: case 0x2c:
		case 0x30: case 0x32: case 0x34: case 0x36:
			return reference(op);
		default:
			return handler_lookup[decoder_index(op)];
		}
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
				if (reference(op) != candidate(op) ||
				reference(op) != candidate_branch(op) ||
				reference(op) != candidate_switch(op))
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
		using fn_t = std::uint64_t(*)(u32);
		constexpr std::array<const char*, 4> names =
		{
			"reference", "mask", "branch", "switch"
		};
		const std::array<fn_t, 4> functions =
		{
			reference, candidate, candidate_branch, candidate_switch
		};
		std::array<std::array<double, pairs>, 4> times{};
		std::array<std::uint64_t, 4> checksums{};

		// Warm each path, rotate trial order on successive pairs.
		for (std::size_t variant = 0; variant < functions.size(); ++variant)
			measure(ops, functions[variant], checksums[variant]);

		for (std::size_t pair = 0; pair < pairs; ++pair)
		{
			for (std::size_t order = 0; order < functions.size(); ++order)
			{
				const std::size_t variant = (order + pair) % functions.size();
				times[variant][pair] = measure(ops, functions[variant], checksums[variant]);
			}

			for (std::size_t variant = 1; variant < functions.size(); ++variant)
			{
				if (checksums[variant] != checksums[0])
				{
					std::cerr << "Checksum mismatch for " << names[variant] << " in " << profile << '\n';
					return false;
				}
			}

			for (std::size_t variant = 0; variant < functions.size(); ++variant)
				raw << profile << ',' << names[variant] << ',' << pair << ','
					<< times[variant][pair] << ',' << checksums[variant] << '\n';
		}

		std::array<double, 4> medians{};
		for (std::size_t variant = 0; variant < functions.size(); ++variant)
		{
			std::sort(times[variant].begin(), times[variant].end());
			medians[variant] = times[variant][pairs / 2];
		}
		std::cout << std::fixed << std::setprecision(3);
		for (std::size_t variant = 0; variant < functions.size(); ++variant)
		{
			std::cout << profile << ": " << names[variant] << "=" << medians[variant]
				<< "ms change=" << 100.0 * (medians[variant] / medians[0] - 1.0)
				<< "% (synthetic proxy, NOT RPCS3 FPS)\n";
		}
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
