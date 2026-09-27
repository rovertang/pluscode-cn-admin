#pragma once
#include "pluscode_admin.h"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

namespace pluscode_admin {
class Error : public std::runtime_error {
public:
    Error(pcad_status status, const std::string& message) : std::runtime_error(message), status_(status) {}
    pcad_status status() const noexcept { return status_; }
private:
    pcad_status status_;
};
struct Options {
    std::size_t cache_capacity = 64;
    std::size_t cache_bytes = 8 * 1024 * 1024;
};
/* One instance serializes its SQLite access/cache. Use independent instances
 * for parallel I/O. Keep the database immutable while an instance is open. */
class Index {
public:
    explicit Index(const std::string& database, Options options = {}) {
        if (database.find('\0') != std::string::npos || options.cache_capacity > 65536)
            throw Error(PCAD_INVALID_ARGUMENT, "Invalid path or cache capacity");
        pcad_options settings{sizeof(pcad_options), static_cast<uint32_t>(options.cache_capacity), options.cache_bytes};
        char* error = nullptr;
        auto status = pcad_open(database.c_str(), &settings, &handle_, &error);
        check(status, error);
    }
    ~Index() { pcad_close(handle_, nullptr); }
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;
    std::string lookup(const std::string& code) {
        if (code.find('\0') != std::string::npos) throw Error(PCAD_INVALID_ARGUMENT, "NUL in Plus Code");
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_lookup_code(handle_, code.c_str(), &data, &error);
        return result(status, data, error);
    }
    std::string lookup_latlng(double latitude, double longitude) {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_lookup_latlng(handle_, latitude, longitude, &data, &error);
        return result(status, data, error);
    }
    std::string metadata() const {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_metadata(handle_, &data, &error);
        return result(status, data, error);
    }
    std::string cache_stats() const {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_cache_stats(handle_, &data, &error);
        return result(status, data, error);
    }
    std::string prefetch_nearby(double latitude, double longitude, int radius_tiles = 1, int max_tiles = 16) {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_prefetch_nearby(handle_, latitude, longitude, radius_tiles, max_tiles, &data, &error);
        return result(status, data, error);
    }
    void clear_cache() {
        char* error = nullptr;
        auto status = pcad_clear_cache(handle_, &error);
        check(status, error);
    }
private:
    pcad_handle handle_ = 0;
    static void check(pcad_status status, char* error) {
        std::unique_ptr<char, decltype(&pcad_free)> owned(error, pcad_free);
        if (status != PCAD_OK) throw Error(status, owned ? owned.get() : "Native operation failed");
    }
    static std::string result(pcad_status status, char* data, char* error) {
        std::unique_ptr<char, decltype(&pcad_free)> owned(data, pcad_free);
        check(status, error);
        return owned.get();
    }
};
}
