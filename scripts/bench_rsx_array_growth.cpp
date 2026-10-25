// Windows-only opt-in experiment on the *actual* RPCS3 rsx::simple_array type.
// Build this twice: unmodified default growth and geometric append growth.
// Output CSV is consumed by the paired CI workflow; checksum is calculated
// after timing so both variants are checked against the same element sequence.
#include "Emu/RSX/Common/simple_array.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <vector>

// The standalone test does not link the emulator's formatting library.
// A failed internal allocation invariant is fatal, as it is in RPCS3;
// successful benchmark paths never call this diagnostic.
namespace fmt
{
	[[noreturn]] void raw_verify_error(std::source_location, const char8_t*, usz)
	{
		std::cerr << "RSX array allocation/invariant failure" << std::endl;
		std::abort();
	}
}

namespace
{
	struct alignas(16) payload64
	{
		std::uint64_t id;
		std::array<std::uint64_t, 7> padding{};
	};

	template <typename T>
	T create_value(std::uint32_t index)
	{
		if constexpr (std::is_same_v<T, std::uint64_t>)
			return static_cast<std::uint64_t>(index ^ 0x5a5a5a5a);
		else
			return payload64{static_cast<std::uint64_t>(index ^ 0x5a5a5a5a), {}};
	}

	template <typename T>
	std::uint64_t get_id(const T& value)
	{
		if constexpr (std::is_same_v<T, std::uint64_t>)
			return value;
		else
			return value.id;
	}

	template <typename T>
	bool run(const char* profile, std::uint32_t count, int mode)
	{
		rsx::simple_array<T> arr;
		std::uint32_t growth_events = 0;
		const auto start = std::chrono::steady_clock::now();

		for (std::uint32_t index = 0; index < count; ++index)
		{
			const auto previous_capacity = arr.capacity();
			if (mode == 0)
			{
				const T value = create_value<T>(index);
				arr.push_back(value);
			}
			else if (mode == 1)
			{
				arr.push_back(create_value<T>(index));
			}
			else
			{
				arr.emplace_back(create_value<T>(index));
			}

			growth_events += arr.capacity() != previous_capacity;
		}

		const auto end = std::chrono::steady_clock::now();
		const double elapsed_ms = std::chrono::duration<double, std::milli>(end - start).count();

		std::uint64_t checksum = 0;
		if (arr.size() != count)
			return false;
		for (std::uint32_t i = 0; i < count; ++i)
		{
			if (get_id(arr[i]) != get_id(create_value<T>(i)))
				return false;
			checksum += get_id(arr[i]);
		}

		const char* operations[] = {"push_lvalue", "push_rvalue", "emplace"};
		std::cout << profile << ',' << operations[mode] << ',' << count << ','
			<< elapsed_ms << ',' << arr.capacity() << ','
			<< growth_events << ',' << checksum << '\n';
		return true;
	}

	// Deterministic property-style check on the production container type:
	// compare mixed operations against std::vector, including transitions
	// between local and heap storage. Capacity is allowed to vary by policy.
	bool check_randomized_sequences()
	{
		std::mt19937 rng(0x1977aabb);
		std::uint64_t checks = 0;

		for (int repetition = 0; repetition < 24; ++repetition)
		{
			rsx::simple_array<std::uint64_t> actual;
			std::vector<std::uint64_t> expected;

			for (int step = 0; step < 900; ++step)
			{
				const unsigned action = rng() % 8;
				const std::uint64_t value = rng();

				switch (action)
				{
				case 0:
					actual.push_back(value);
					expected.push_back(value);
					break;
				case 1:
					actual.push_back(std::uint64_t{value});
					expected.push_back(value);
					break;
				case 2:
					actual.emplace_back(value);
					expected.emplace_back(value);
					break;
				case 3:
				{
					const std::size_t pos = expected.empty() ? 0 : rng() % (expected.size() + 1);
					actual.insert(actual.begin() + pos, value);
					expected.insert(expected.begin() + pos, value);
					break;
				}
				case 4:
					if (!expected.empty())
					{
						if (actual.pop_back() != expected.back()) return false;
						expected.pop_back();
					}
					break;
				case 5:
					if (step % 37 == 0)
					{
						actual.clear();
						expected.clear();
					}
					break;
				case 6:
					// Exact reserve remains independent of append growth.
					actual.reserve(static_cast<u32>(expected.size() + (rng() % 24)));
					break;
				case 7:
				{
					const rsx::simple_array<std::uint64_t> copied(actual);
					if (copied.size() != expected.size()) return false;
					for (std::size_t i = 0; i < expected.size(); ++i)
						if (copied[static_cast<u32>(i)] != expected[i]) return false;
					break;
				}
				}

				if (actual.size() != expected.size()) return false;
				if (actual.capacity() < actual.size()) return false;
				for (std::size_t i = 0; i < expected.size(); ++i)
				{
					if (actual[static_cast<u32>(i)] != expected[i]) return false;
					checks += actual[static_cast<u32>(i)];
				}
			}
		}

		std::cerr << "PASS: randomized sequence correctness, signature=" << checks << '\n';
		return true;
	}

}

int main()
{
	if (!check_randomized_sequences())
	{
		std::cerr << "FAIL: randomized sequence semantics mismatch" << std::endl;
		return 3;
	}

	std::cout << "profile,operation,count,elapsed_ms,capacity,growth_events,checksum\n";
	for (int mode = 0; mode < 3; ++mode)
	{
		if (!run<std::uint64_t>("u64_8192", 8192, mode)) return 1;
		if (!run<std::uint64_t>("u64_65536", 65536, mode)) return 1;
		if (!run<payload64>("payload64_8192", 8192, mode)) return 1;
		if (!run<payload64>("payload64_32768", 32768, mode)) return 1;
	}
	return 0;
}
