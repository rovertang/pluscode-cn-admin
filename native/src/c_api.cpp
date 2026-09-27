#include "engine.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>

namespace {
std::mutex handles_mutex;
std::map<pcad_handle, std::shared_ptr<pluscode_admin::Engine>> handles;
pcad_handle next_handle = 1;
char* duplicate(const std::string& text) {
    auto* memory = static_cast<char*>(std::malloc(text.size() + 1));
    if (!memory) throw std::bad_alloc();
    std::memcpy(memory, text.c_str(), text.size() + 1);
    return memory;
}
std::shared_ptr<pluscode_admin::Engine> get(pcad_handle handle) {
    std::lock_guard<std::mutex> guard(handles_mutex);
    auto found = handles.find(handle);
    if (found == handles.end()) throw pluscode_admin::Error(PCAD_CLOSED, "Invalid or closed index handle");
    return found->second;
}
template<class Function> pcad_status checked(char** error, Function function) noexcept {
    if (error) *error = nullptr;
    pcad_status status;
    const char* message = "Unknown native error";
    try { function(); return PCAD_OK; }
    catch (const pluscode_admin::Error& failure) {
        status = failure.status();
        if (error) { try { *error = duplicate(failure.what()); } catch (...) {} }
        return status;
    } catch (const std::exception& failure) {
        if (error) { try { *error = duplicate(failure.what()); } catch (...) {} }
        return PCAD_INTERNAL_ERROR;
    } catch (...) {
        if (error) { try { *error = duplicate(message); } catch (...) {} }
        return PCAD_INTERNAL_ERROR;
    }
}
void require(bool valid, const char* message) {
    if (!valid) throw pluscode_admin::Error(PCAD_INVALID_ARGUMENT, message);
}
template<class Function> pcad_status result(char** output, char** error, Function function) {
    if (output) *output = nullptr;
    return checked(error, [&] { require(output && output != error, "Distinct result/error outputs required"); *output = duplicate(function()); });
}
}

extern "C" {
uint32_t pcad_abi_version(void) { return 1; }
const char* pcad_version(void) { return "1.0.0"; }
void pcad_free(void* memory) { std::free(memory); }
pcad_status pcad_open(const char* database, const pcad_options* options, pcad_handle* handle, char** error) {
    if (handle) *handle = 0;
    return checked(error, [&] {
        require(database && handle, "Database path and handle output required");
        pluscode_admin::Options settings;
        if (options) {
            require(options->struct_size == sizeof(pcad_options), "Incompatible options structure");
            require(options->cache_bytes <= std::numeric_limits<std::size_t>::max(), "Cache byte limit overflows size_t");
            settings = {options->cache_capacity, static_cast<std::size_t>(options->cache_bytes)};
        }
        auto index = std::make_shared<pluscode_admin::Engine>(database, settings);
        std::lock_guard<std::mutex> guard(handles_mutex);
        require(next_handle != 0, "Native handle IDs exhausted");
        auto id = next_handle++;
        handles.emplace(id, std::move(index));
        *handle = id;
    });
}
pcad_status pcad_lookup_code(pcad_handle handle, const char* code, char** json, char** error) {
    return result(json, error, [&] { require(code != nullptr, "Plus Code is required"); return get(handle)->lookup(code); });
}
pcad_status pcad_lookup_latlng(pcad_handle handle, double latitude, double longitude, char** json, char** error) {
    return result(json, error, [&] { return get(handle)->lookup_latlng(latitude, longitude); });
}
pcad_status pcad_metadata(pcad_handle handle, char** json, char** error) {
    return result(json, error, [&] { return get(handle)->metadata(); });
}
pcad_status pcad_cache_stats(pcad_handle handle, char** json, char** error) {
    return result(json, error, [&] { return get(handle)->cache_stats(); });
}
pcad_status pcad_prefetch_nearby(pcad_handle handle, double latitude, double longitude,
                                int radius_tiles, int max_tiles, char** json, char** error) {
    return result(json, error, [&] { return get(handle)->prefetch_nearby(latitude, longitude, radius_tiles, max_tiles); });
}
pcad_status pcad_clear_cache(pcad_handle handle, char** error) {
    return checked(error, [&] { get(handle)->clear_cache(); });
}
pcad_status pcad_close(pcad_handle handle, char** error) {
    return checked(error, [&] {
        std::shared_ptr<pluscode_admin::Engine> removed;
        {
            std::lock_guard<std::mutex> guard(handles_mutex);
            auto found = handles.find(handle);
            if (found == handles.end()) throw pluscode_admin::Error(PCAD_CLOSED, "Invalid or closed index handle");
            removed = std::move(found->second);
            handles.erase(found);
        }
    });
}
}
