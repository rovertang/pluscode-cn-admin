#pragma once
#include "pluscode_admin/index.hpp"

namespace pluscode_admin {
class Engine {
public:
    explicit Engine(const std::string& database, Options options);
    ~Engine();
    std::string lookup(const std::string& code);
    std::string lookup_latlng(double latitude, double longitude);
    std::string metadata() const;
    std::string cache_stats();
    std::string prefetch_nearby(double latitude, double longitude, int radius_tiles, int max_tiles);
    void clear_cache();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
