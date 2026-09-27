#include "pluscode_admin/index.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

void expect(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "Database path required");
        using Json = nlohmann::json;
        pluscode_admin::Index index(argv[1], {2, 1024 * 1024});
        expect(Json::parse(index.cache_stats())["loads"] == 0, "Open eagerly loaded tiles");
        auto result = Json::parse(index.lookup(" 8pfrwc34+mx2 "));
        expect(result["admin"]["county_code"] == "110101", "Beijing mismatch");
        expect(result["matched_length"] == 8, "Large leaf mismatch");
        expect(Json::parse(index.lookup("8P67C2C9+GP"))["status"] == "ambiguous", "Coarse input must be ambiguous");
        expect(Json::parse(index.lookup("8P67C2C9+GP422"))["precision"] == 11, "Long code truncation");
        expect(Json::parse(index.lookup_latlng(48.8566, 2.3522))["status"] == "outside_coverage", "Outside coverage mismatch");
        auto warm = Json::parse(index.prefetch_nearby(39.9042, 116.4074, 10, 64));
        expect(warm["visited_tiles"].get<int>() <= 2, "Prefetch exceeds cache capacity");
        auto stats = Json::parse(index.cache_stats());
        expect(stats["cached_tiles"].get<int>() <= 2 && stats["cached_bytes"].get<int>() <= 1024 * 1024, "Cache bounds exceeded");
        index.clear_cache();
        expect(Json::parse(index.cache_stats())["cached_bytes"] == 0, "Clear did not release cache");
        index.lookup("8P67C2C9+GP4");
        auto loads = Json::parse(index.cache_stats())["loads"];
        index.lookup("8P67C2C9+GP4");
        expect(Json::parse(index.cache_stats())["loads"] == loads, "Hot query reloaded tile");
        pluscode_admin::Index uncached(argv[1], {0, 0});
        expect(Json::parse(uncached.prefetch_nearby(39.9, 116.4, 1, 16))["visited_tiles"] == 0, "Disabled prefetch loaded tiles");
        expect(uncached.lookup("8P67C2C9+GP4") == index.lookup("8P67C2C9+GP4"), "Uncached result mismatch");
        expect(Json::parse(uncached.cache_stats())["cached_tiles"] == 0, "Disabled cache retained tiles");
        for (int radius : {-1, 21}) {
            try { index.prefetch_nearby(0, 0, radius, 16); throw std::runtime_error("Invalid prefetch accepted"); }
            catch (const pluscode_admin::Error& error) { expect(error.status() == PCAD_INVALID_ARGUMENT, "Wrong prefetch error"); }
        }
        index.prefetch_nearby(90, 180, 1, 16);
        index.prefetch_nearby(-90, -180, 1, 16);
        for (const char* code : {"BAD", "WC34+MX", "8PFR0000+", "8PFRWC34+M", "8PFRWC34+MX0"}) {
            try { index.lookup(code); throw std::runtime_error("Accepted invalid code"); }
            catch (const pluscode_admin::Error& error) { expect(error.status() == PCAD_INVALID_ARGUMENT, "Wrong invalid-code error"); }
        }
        for (double value : {91.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
            try { index.lookup_latlng(value, 0); throw std::runtime_error("Accepted invalid coordinate"); }
            catch (const pluscode_admin::Error& error) { expect(error.status() == PCAD_INVALID_ARGUMENT, "Wrong coordinate error"); }
        }
        pcad_handle handle = 0;
        char* error = nullptr;
        expect(pcad_open(argv[1], nullptr, &handle, &error) == PCAD_OK, "C ABI open failed");
        pcad_free(error);
        std::atomic<int> failures{0};
        std::vector<std::thread> workers;
        for (int i = 0; i < 8; ++i) workers.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                char* text = nullptr; char* message = nullptr;
                auto status = pcad_lookup_code(handle, "8P67C2C9+GP4", &text, &message);
                if (status != PCAD_OK || !text) ++failures;
                pcad_free(text); pcad_free(message);
            }
        });
        for (auto& worker : workers) worker.join();
        expect(failures == 0, "Concurrent lookup failed");
        expect(pcad_close(handle, &error) == PCAD_OK, "C ABI close failed"); pcad_free(error);
        char* text = nullptr;
        expect(pcad_lookup_code(handle, "8PFRWC34+MX2", &text, &error) == PCAD_CLOSED, "Closed handle accepted");
        expect(text == nullptr, "Error returned result pointer"); pcad_free(error);
        expect(pcad_close(handle, &error) == PCAD_CLOSED, "Double close accepted"); pcad_free(error);
        expect(pcad_lookup_code(0, nullptr, nullptr, &error) == PCAD_INVALID_ARGUMENT, "Null arguments accepted"); pcad_free(error);
        std::cout << "Native contracts and 800 concurrent lookups passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
