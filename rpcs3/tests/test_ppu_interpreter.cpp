#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>

#include "util/types.hpp"
#include "Emu/Cell/PPUInterpreter.h"

TEST(PPUInterpreterPerformance, DecodeThroughput)
{
	ppu_interpreter_rt decoder;

	// A repeatable mix of common PPU instruction encodings. Decode only: no
	// emulated state is touched and the selected handlers are never executed.
	constexpr std::array<u32, 16> instructions =
	{
		0x38600001u, // addi
		0x3c600000u, // lis
		0x60000000u, // ori
		0x64000000u, // oris
		0x7c632214u, // add
		0x7c0802a6u, // mflr
		0x4e800020u, // blr
		0x48000000u, // b
		0x41820010u, // conditional branch
		0x80630000u, // lwz
		0x90630000u, // stw
		0x88630000u, // lbz
		0x98630000u, // stb
		0xa0630000u, // lhz
		0xb0630000u, // sth
		0x1000002cu, // vector instruction encoding
	};

	for (const u32 opcode : instructions)
	{
		EXPECT_NE(decoder.decode(opcode), nullptr);
	}

	constexpr u64 iterations = 2'000'000;
	constexpr usz sample_count = 7;
	std::array<double, sample_count> elapsed_samples{};
	u64 checksum = 0;

	for (double& sample : elapsed_samples)
	{
		const auto start = std::chrono::steady_clock::now();

		for (u64 i = 0; i < iterations; i++)
		{
			checksum += reinterpret_cast<uptr>(decoder.decode(instructions[i & (instructions.size() - 1)]));
		}

		sample = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	std::sort(elapsed_samples.begin(), elapsed_samples.end());
	const double median_seconds = elapsed_samples[sample_count / 2];
	const double median_ops_per_second = static_cast<double>(iterations) / median_seconds;
	const double fastest_ops_per_second = static_cast<double>(iterations) / elapsed_samples.front();
	const double slowest_ops_per_second = static_cast<double>(iterations) / elapsed_samples.back();

	std::cerr << "[PERF] PPU interpreter decode: " << iterations << " iterations x " << sample_count
		<< " samples; median=" << median_ops_per_second << " decodes/s"
		<< ", range=" << slowest_ops_per_second << ".." << fastest_ops_per_second
		<< " decodes/s, checksum=" << checksum << '\n';

	EXPECT_NE(checksum, 0u);
}
