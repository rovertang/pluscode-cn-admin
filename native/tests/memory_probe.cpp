#include "pluscode_admin/index.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <sys/resource.h>

long rss_kib() {
    std::ifstream status("/proc/self/status");
    std::string field;
    while (status >> field) {
        if (field == "VmRSS:") { long value; status >> value; return value; }
        std::getline(status, field);
    }
    return -1;
}
void report(const char* phase, pluscode_admin::Index* index, double elapsed = 0) {
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    std::cout << "{\"phase\":\"" << phase << "\",\"rss_kib\":" << rss_kib()
              << ",\"peak_rss_kib\":" << usage.ru_maxrss << ",\"elapsed_ms\":" << elapsed
              << ",\"cache\":" << (index ? index->cache_stats() : "null") << "}\n";
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        using Clock = std::chrono::steady_clock;
        report("baseline", nullptr);
        auto start = Clock::now();
        pluscode_admin::Index index(argv[1]);
        auto elapsed = [&] { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
        report("open", &index, elapsed());
        start = Clock::now();
        for (int i = 0; i < 10000; ++i) index.lookup("8P67C2C9+GP4");
        report("hot_10000", &index, elapsed());
        start = Clock::now();
        for (int i = 0; i < 2000; ++i) index.lookup_latlng(39.9 + i * 0.0002, 116.4 + i * 0.0003);
        report("moving_2000", &index, elapsed());
        start = Clock::now();
        index.prefetch_nearby(39.9, 116.4, 10, 64);
        report("prefetch_radius_10", &index, elapsed());
        start = Clock::now();
        std::mt19937 random(20260907);
        std::uniform_real_distribution<double> latitude(18, 54), longitude(73, 135);
        for (int i = 0; i < 3000; ++i) index.lookup_latlng(latitude(random), longitude(random));
        report("random_china_3000", &index, elapsed());
        index.clear_cache();
        report("cleared", &index);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
