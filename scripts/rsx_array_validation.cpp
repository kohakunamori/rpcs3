// Isolated checks and workloads using the actual RPCS3 container header.
// Run correctness separately from timings; ASan timings are never benchmarks.
#include "Emu/RSX/Common/simple_array.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fmt
{
	// Standalone fatal diagnostic only; no replacement allocator or container.
	[[noreturn]] void raw_verify_error(std::source_location, const char8_t*, usz)
	{
		std::cerr << "RPCS3 container invariant failed\n";
		std::abort();
	}
}

#ifdef _MSC_VER
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

namespace
{
	constexpr std::array<u32, 13> sizes{0, 1, 8, 9, 16, 17, 24, 25, 64, 65, 256, 1024, 8192};
	constexpr bool geometric =
#ifdef RPCS3_EXPERIMENTAL_RSX_ARRAY_GEOMETRIC_GROWTH
		true;
#else
		false;
#endif
	constexpr const char* policy = geometric ? "grow25" : "baseline";
#ifdef RPCS3_RSX_ARRAY_GROWTH_DIVISOR
	static_assert(RPCS3_RSX_ARRAY_GROWTH_DIVISOR == 4, "This validation targets 1.25x only");
#endif

	struct alignas(64) record64
	{
		std::array<u64, 8> words{};
		bool operator==(const record64&) const = default;
	};
	static_assert(sizeof(record64) == 64);

	void require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	template <typename T> T value(u64 seed)
	{
		if constexpr (std::is_same_v<T, u64>) return seed;
		else
		{
			record64 result;
			for (usz i = 0; i < result.words.size(); ++i) result.words[i] = seed + i;
			return result;
		}
	}

	template <typename T> u64 id(const T& element)
	{
		if constexpr (std::is_same_v<T, u64>) return element;
		else return element.words[0];
	}

	template <typename T> void equal(const rsx::simple_array<T>& array, const std::vector<T>& expected)
	{
		require(array.size() == expected.size(), "Size mismatch");
		require(array.capacity() >= array.size(), "Capacity too small");
		require(reinterpret_cast<usz>(array.data()) % alignof(T) == 0, "Misaligned storage");
		for (u32 i = 0; i < array.size(); ++i) require(array[i] == expected[i], "Element mismatch");
	}

	u32 next_capacity(u32 old)
	{
		return old + (geometric ? std::max<u32>(16, old / 4) : 16);
	}

	template <typename T> void verify_type()
	{
		for (u32 count : sizes)
		{
			rsx::simple_array<T> array;
			std::vector<T> expected;
			u32 capacity = array.capacity();
			for (u32 i = 0; i < count; ++i)
			{
				if (i == capacity) capacity = next_capacity(capacity);
				const T item = value<T>(i + 1);
				if (i % 3 == 0) array.push_back(item);
				else if (i % 3 == 1) array.push_back(value<T>(i + 1));
				else array.emplace_back(item);
				expected.push_back(item);
				require(array.capacity() == capacity, "Wrong compiled capacity policy");
			}
			equal(array, expected);

			// Both insert overloads and independent span input; no self-aliasing.
			const T first = value<T>(90001);
			auto inserted = array.insert(array.begin(), first);
			expected.insert(expected.begin(), first);
			require(inserted == array.begin(), "Wrong insert return value");
			equal(array, expected);
			const u32 middle = array.size() / 2;
			array.insert(array.begin() + middle, value<T>(90002));
			expected.insert(expected.begin() + middle, value<T>(90002));
			equal(array, expected);
			const std::array<T, 2> tail{value<T>(90003), value<T>(90004)};
			array.insert(array.end(), tail);
			expected.insert(expected.end(), tail.begin(), tail.end());
			equal(array, expected);

			// Copy/move/swap are tested according to this container's contract.
			rsx::simple_array<T> copy(array);
			equal(copy, expected);
			rsx::simple_array<T> moved(std::move(copy));
			equal(moved, expected);
			require(copy.empty(), "Move construction did not empty source");
			rsx::simple_array<T> small;
			small.push_back(first);
			moved.swap(small);
			equal(small, expected);
			equal(moved, std::vector<T>{first});
			array = small;
			equal(array, expected);

			const u32 requested = array.capacity() + 37;
			array.reserve(requested);
			require(array.capacity() == requested, "Explicit reserve rounded capacity");
			equal(array, expected);
			array.reserve(1);
			require(array.capacity() == requested, "Reserve unexpectedly shrank capacity");
			T* storage = array.data();
			array.clear();
			require(array.empty() && array.data() == storage, "Clear discarded storage");
			for (u32 i = 0; i < count; ++i) array.push_back(value<T>(i));
			require(array.data() == storage, "Reuse unexpectedly allocated");
			while (!array.empty())
			{
				const u32 index = array.size() - 1;
				require(array.pop_back() == value<T>(index), "Pop order changed");
			}
		}
	}

	template <typename T> BENCH_NOINLINE void fill(rsx::simple_array<T>& array, u32 count, u64 seed)
	{
		array.clear();
		for (u32 i = 0; i < count; ++i) array.push_back(value<T>(seed + i));
	}

	// Separate non-inlined consumer prevents dead-store/unused-work elimination.
	template <typename T> BENCH_NOINLINE u64 consume(const rsx::simple_array<T>& array)
	{
		u64 result = 0;
		for (const auto& element : array) result += id(element);
		return result;
	}

	template <typename T> void benchmark(const char* type, u32 count, const std::string& mode, u32 batch_scale)
	{
		const u32 repeats = std::clamp<u32>(262144 / (count + 1), 16, 32768) * batch_scale;
		rsx::simple_array<T> reusable;
		if (mode == "reuse") fill(reusable, count, 1);
		if (mode == "reserved") reusable.reserve(count);

		// Untimed capacity/growth telemetry; never add this counter to the hot loop.
		rsx::simple_array<T> probe;
		if (mode == "reserved") probe.reserve(count);
		if (mode == "reuse") fill(probe, count, 1);
		probe.clear();
		u32 growths = 0;
		for (u32 i = 0; i < count; ++i)
		{
			const u32 before = probe.capacity();
			probe.push_back(value<T>(i));
			growths += before != probe.capacity();
		}

		u64 checksum = 0;
		const auto start = std::chrono::steady_clock::now();
		for (u32 round = 0; round < repeats; ++round)
		{
			if (mode == "cold")
			{
				rsx::simple_array<T> fresh;
				fill(fresh, count, round + 1);
				checksum += consume(fresh);
			}
			else
			{
				fill(reusable, count, round + 1);
				checksum += consume(reusable);
			}
		}
		const auto elapsed = std::chrono::steady_clock::now() - start;
		const double ms = std::chrono::duration<double, std::milli>(elapsed).count();
		const u64 expected = u64{count} * repeats * (u64{count} + repeats) / 2;
		require(checksum == expected, "Benchmark checksum mismatch");
		std::cout << policy << ',' << type << ',' << mode << ',' << count << ',' << repeats << ','
			<< std::setprecision(12) << ms << ',' << checksum << ',' << probe.capacity() << ','
			<< u64{probe.capacity()} * sizeof(T) << ',' << growths << '\n';
	}
}

int main(int argc, char** argv)
{
	try
	{
		if (argc == 2 && std::string(argv[1]) == "--verify-only")
		{
			verify_type<u64>();
			verify_type<record64>();
			std::cout << "PASS boundary/content/alignment checks policy=" << policy
				<< " type_size_cases=" << sizes.size() * 2 << '\n';
			return 0;
		}
		if ((argc != 2 && argc != 3) || std::string(argv[1]) != "--benchmark")
		{
			std::cerr << "Usage: rsx_array_validation --verify-only|--benchmark [batch_scale:1..1024]\n";
			return 2;
		}
		u32 batch_scale = 1;
		if (argc == 3)
		{
			const std::string text(argv[2]);
			require(!text.empty() && text.find_first_not_of("0123456789") == std::string::npos, "Invalid batch scale");
			const unsigned long parsed = std::stoul(text);
			require(parsed >= 1 && parsed <= 1024, "Batch scale out of bounds");
			batch_scale = static_cast<u32>(parsed);
		}
		std::cout << "policy,type,mode,count,repeats,elapsed_ms,checksum,capacity,reserved_bytes,growths\n";
		for (u32 count : sizes)
			for (const std::string mode : {"cold", "reserved", "reuse"})
			{
				benchmark<u64>("u64", count, mode, batch_scale);
				benchmark<record64>("record64", count, mode, batch_scale);
			}
		return 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << "FAIL " << e.what() << '\n';
		return 1;
	}
}
