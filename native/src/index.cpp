#include "engine.hpp"
#include <sqlite3.h>
#include <lzma.h>
#include <zlib.h>
#include <nlohmann/json.hpp>
#include <openlocationcode.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

namespace pluscode_admin {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<uint8_t>;
constexpr uint16_t branch = 0xffff, label_mask = 0x3fff, boundary = 0x8000, overlap = 0x4000;
constexpr std::size_t max_raw = 64 * 1024 * 1024;
constexpr char alphabet[] = "23456789CFGHJMPQRVWX";
constexpr char format[] = "olc-adaptive-4-6-8-10-11-v2";
[[noreturn]] void corrupt(const std::string& text) { throw Error(PCAD_INVALID_INDEX, text); }
int digit(char value) {
    const char* found = std::strchr(alphabet, value);
    if (!found || !value) throw Error(PCAD_INVALID_ARGUMENT, "Invalid Plus Code character");
    return static_cast<int>(found - alphabet);
}
int child_index(const std::string& raw, int level) {
    return level == 10 ? digit(raw.at(10)) : digit(raw.at(level)) * 20 + digit(raw.at(level + 1));
}
std::string normalized(std::string code) {
    if (code.size() > 256) throw Error(PCAD_INVALID_ARGUMENT, "Plus Code exceeds 256 bytes");
    const auto first = code.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) throw Error(PCAD_INVALID_ARGUMENT, "Empty Plus Code");
    code = code.substr(first, code.find_last_not_of(" \t\r\n\f\v") - first + 1);
    for (char& c : code) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (!openlocationcode::IsFull(code) || code.find('0') != std::string::npos)
        throw Error(PCAD_INVALID_ARGUMENT, "A full Plus Code with at least 8 significant characters is required");
    return code;
}
struct Database {
    sqlite3* value = nullptr;
    ~Database() { if (value) sqlite3_close_v2(value); }
};
struct Statement {
    sqlite3_stmt* value = nullptr;
    Statement(sqlite3* db, const char* sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK) corrupt(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(value); }
    bool row() {
        const int status = sqlite3_step(value);
        if (status == SQLITE_ROW) return true;
        if (status != SQLITE_DONE) corrupt(sqlite3_errmsg(sqlite3_db_handle(value)));
        return false;
    }
    std::string text(int col) const {
        if (sqlite3_column_type(value, col) != SQLITE_TEXT) corrupt("Expected TEXT column");
        const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(value, col));
        const auto length = sqlite3_column_bytes(value, col);
        if (!data) corrupt("Unable to read TEXT column");
        return std::string(data, static_cast<std::size_t>(length));
    }
    Bytes blob(int col) const {
        if (sqlite3_column_type(value, col) != SQLITE_BLOB) corrupt("Expected BLOB column");
        const auto length = sqlite3_column_bytes(value, col);
        if (length <= 0 || length > 16 * 1024 * 1024) corrupt("Invalid compressed block size");
        const auto* data = static_cast<const uint8_t*>(sqlite3_column_blob(value, col));
        if (!data) corrupt("Unable to read BLOB column");
        return Bytes(data, data + length);
    }
};
struct Cursor {
    const Bytes& bytes;
    std::size_t pos, end;
    Cursor(const Bytes& input, std::size_t begin, std::size_t stop) : bytes(input), pos(begin), end(stop) {
        if (begin > stop || stop > input.size()) corrupt("Invalid subtree span");
    }
    uint32_t read(std::size_t count) {
        if (count > end - pos) corrupt("Truncated subtree");
        uint32_t value = 0;
        for (std::size_t i = 0; i < count; ++i) value |= uint32_t(bytes[pos++]) << (i * 8);
        return value;
    }
    void skip(std::size_t count) {
        if (count > end - pos) corrupt("Child span exceeds parent");
        pos += count;
    }
};
std::vector<uint16_t> vector_at(Cursor& cursor, std::size_t size) {
    const auto mode = cursor.read(1);
    std::vector<uint16_t> values(size);
    if (mode == 0) {
        const auto count = cursor.read(2);
        std::size_t previous = 0;
        for (uint32_t i = 0; i < count; ++i) {
            const auto stop = cursor.read(2), value = cursor.read(2);
            if (stop <= previous || stop > size) corrupt("Invalid RLE coverage");
            std::fill(values.begin() + previous, values.begin() + stop, static_cast<uint16_t>(value));
            previous = stop;
        }
        if (previous != size) corrupt("Incomplete RLE coverage");
    } else if (mode == 1) {
        const auto default_value = cursor.read(2), count = cursor.read(2);
        if (count > size) corrupt("Invalid sparse count");
        std::fill(values.begin(), values.end(), static_cast<uint16_t>(default_value));
        int previous = -1;
        for (uint32_t i = 0; i < count; ++i) {
            const auto index = cursor.read(2), value = cursor.read(2);
            if (index >= size || static_cast<int>(index) <= previous) corrupt("Invalid sparse position");
            values[index] = static_cast<uint16_t>(value);
            previous = static_cast<int>(index);
        }
    } else corrupt("Unknown vector encoding");
    return values;
}
Bytes inflate_directory(const Bytes& input) {
    Bytes result(4096);
    uLongf length = static_cast<uLongf>(result.size());
    uLong source_length = static_cast<uLong>(input.size());
    if (uncompress2(result.data(), &length, input.data(), &source_length) != Z_OK || source_length != input.size())
        corrupt("Invalid zlib directory");
    result.resize(length);
    return result;
}
Bytes inflate_tile(const Bytes& input) {
    lzma_stream stream = LZMA_STREAM_INIT;
    if (lzma_stream_decoder(&stream, 128 * 1024 * 1024, 0) != LZMA_OK) corrupt("Cannot initialize XZ decoder");
    struct End { lzma_stream* stream; ~End() { lzma_end(stream); } } end{&stream};
    stream.next_in = input.data();
    stream.avail_in = input.size();
    Bytes result;
    std::array<uint8_t, 65536> chunk{};
    for (;;) {
        stream.next_out = chunk.data();
        stream.avail_out = chunk.size();
        const auto status = lzma_code(&stream, LZMA_FINISH);
        const auto produced = chunk.size() - stream.avail_out;
        if (produced > max_raw - result.size()) corrupt("Decoded tile exceeds 64 MiB limit");
        result.insert(result.end(), chunk.begin(), chunk.begin() + produced);
        if (status == LZMA_STREAM_END) {
            if (stream.avail_in != 0 || result.empty()) corrupt("Trailing or empty XZ block");
            return result;
        }
        if (status != LZMA_OK || (produced == 0 && stream.avail_in == 0)) corrupt("Invalid or truncated XZ block");
    }
}
struct Summary { std::set<uint16_t> labels; uint16_t flags = 0; };
void scan(Cursor& cursor, int level, const std::map<int, Json>& admins, Summary* summary = nullptr) {
    const auto tag = cursor.read(1);
    auto leaf = [&](uint16_t value, int leaf_level) {
        const int id = value & label_mask;
        if (value == branch || (id && !admins.count(id))) corrupt("Unknown administrative ID");
        if (leaf_level < 11 && (value & (boundary | overlap))) corrupt("Flags on certified coarse leaf");
        if (summary) { summary->labels.insert(static_cast<uint16_t>(id)); summary->flags |= value & (boundary | overlap); }
    };
    if (tag == 0) { leaf(static_cast<uint16_t>(cursor.read(2)), level); return; }
    if (tag != 1 || (level != 6 && level != 8 && level != 10)) corrupt("Invalid tree node");
    const auto values = vector_at(cursor, level == 10 ? 20 : 400);
    const int child_level = level == 10 ? 11 : level + 2;
    for (auto value : values) if (value != branch) leaf(value, child_level);
    for (auto value : values) if (value == branch) {
        if (level == 10) corrupt("Refinement past 11 digits");
        const auto length = cursor.read(4);
        const auto start = cursor.pos;
        cursor.skip(length);
        Cursor child(cursor.bytes, start, cursor.pos);
        scan(child, child_level, admins, summary);
        if (child.pos != child.end) corrupt("Trailing subtree bytes");
    }
}
}

struct Engine::Impl {
    struct Root { int owner = -1; std::vector<uint16_t> values; };
    struct Cached { std::shared_ptr<const Bytes> bytes; std::list<std::string>::iterator recency; };
    Database db;
    std::unique_ptr<Statement> tile_statement;
    Json meta;
    std::map<int, Json> admins;
    std::unordered_map<std::string, Root> roots;
    Options options;
    std::mutex mutex;
    std::unordered_map<std::string, Cached> cache;
    std::list<std::string> recency;
    std::size_t cached_bytes = 0;
    uint64_t hits = 0, misses = 0, loads = 0, evictions = 0, compressed_bytes_read = 0;

    Impl(const std::string& path, Options limits) : options(limits) {
        if (path.empty() || path.find('\0') != std::string::npos) throw Error(PCAD_INVALID_ARGUMENT, "Invalid database path");
        if (limits.cache_capacity > 65536 || limits.cache_bytes > uint64_t(4) * 1024 * 1024 * 1024)
            throw Error(PCAD_INVALID_ARGUMENT, "Cache limits exceed supported bounds");
        if (sqlite3_open_v2(path.c_str(), &db.value, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
            throw Error(PCAD_IO_ERROR, db.value ? sqlite3_errmsg(db.value) : "Cannot open database");
        sqlite3_limit(db.value, SQLITE_LIMIT_LENGTH, 16 * 1024 * 1024);
        if (sqlite3_exec(db.value, "PRAGMA trusted_schema=OFF; PRAGMA query_only=ON; PRAGMA mmap_size=0; PRAGMA cache_size=-2048", nullptr, nullptr, nullptr) != SQLITE_OK)
            corrupt(sqlite3_errmsg(db.value));
        Statement metadata(db.value, "SELECT key,value FROM metadata");
        meta = Json::object();
        while (metadata.row()) meta[metadata.text(0)] = Json::parse(metadata.text(1));
        if (!meta.contains("format") || meta.at("format") != format || !meta.contains("complete") || meta.at("complete") != true)
            corrupt("Incomplete or incompatible V2 index");
        Statement dictionary(db.value, "SELECT id,value FROM admins");
        while (dictionary.row()) {
            const auto id = sqlite3_column_int64(dictionary.value, 0);
            if (sqlite3_column_type(dictionary.value, 0) != SQLITE_INTEGER || id <= 0 || id >= label_mask) corrupt("Invalid dictionary ID");
            Json admin = Json::parse(dictionary.text(1));
            if (!admin.is_object() || admin.value("admin_id", 0) != id || !admins.emplace(static_cast<int>(id), std::move(admin)).second)
                corrupt("Inconsistent administrative dictionary");
        }
        if (admins.empty()) corrupt("Empty administrative dictionary");
        Statement directory(db.value, "SELECT key,owner,value FROM roots");
        while (directory.row()) {
            const auto key = directory.text(0);
            if (key.size() != 4 || key.find_first_not_of(alphabet) != std::string::npos || !openlocationcode::IsFull(key + "0000+")) corrupt("Invalid root prefix");
            Root root;
            if (sqlite3_column_type(directory.value, 1) != SQLITE_NULL) {
                const auto owner = sqlite3_column_int64(directory.value, 1);
                if (sqlite3_column_type(directory.value, 1) != SQLITE_INTEGER || owner < 0 || owner >= label_mask || (owner && !admins.count(static_cast<int>(owner))) || sqlite3_column_type(directory.value, 2) != SQLITE_NULL)
                    corrupt("Invalid root owner");
                root.owner = static_cast<int>(owner);
            } else {
                const auto raw = inflate_directory(directory.blob(2));
                Cursor cursor(raw, 0, raw.size());
                root.values = vector_at(cursor, 400);
                if (cursor.pos != cursor.end) corrupt("Trailing directory data");
                for (auto value : root.values)
                    if (value != branch && (value & (boundary | overlap) || (value && !admins.count(value)))) corrupt("Invalid directory value");
            }
            if (!roots.emplace(key, std::move(root)).second) corrupt("Duplicate root prefix");
        }
        if (roots.empty()) corrupt("Empty root directory");
        tile_statement = std::make_unique<Statement>(db.value, "SELECT value FROM tiles WHERE key=?1");
    }

    std::shared_ptr<const Bytes> tile(const std::string& key) {
        auto existing = cache.find(key);
        if (existing != cache.end()) {
            ++hits;
            recency.splice(recency.begin(), recency, existing->second.recency);
            return existing->second.bytes;
        }
        ++misses;
        sqlite3_reset(tile_statement->value);
        sqlite3_clear_bindings(tile_statement->value);
        if (sqlite3_bind_text(tile_statement->value, 1, key.c_str(), static_cast<int>(key.size()), SQLITE_TRANSIENT) != SQLITE_OK)
            corrupt("Cannot bind tile key");
        struct Reset { sqlite3_stmt* statement; ~Reset() { sqlite3_reset(statement); } } reset{tile_statement->value};
        if (!tile_statement->row()) corrupt("Required tile is missing: " + key);
        const auto compressed = tile_statement->blob(0);
        compressed_bytes_read += compressed.size();
        auto raw = std::make_shared<Bytes>(inflate_tile(compressed));
        Cursor cursor(*raw, 0, raw->size());
        scan(cursor, 6, admins);
        if (cursor.pos != cursor.end) corrupt("Trailing tree data");
        ++loads;
        if (options.cache_capacity && raw->capacity() <= options.cache_bytes) {
            while (!recency.empty() && (cache.size() >= options.cache_capacity || cached_bytes > options.cache_bytes - raw->capacity())) {
                auto found = cache.find(recency.back());
                cached_bytes -= found->second.bytes->capacity();
                cache.erase(found);
                recency.pop_back();
                ++evictions;
            }
            recency.push_front(key);
            try { cache.emplace(key, Cached{raw, recency.begin()}); }
            catch (...) { recency.pop_front(); throw; }
            cached_bytes += raw->capacity();
        }
        return raw;
    }

    Json leaf_result(const std::string& code, const std::string& raw, int level, uint16_t value) const {
        const auto id = value & label_mask;
        auto prefix = raw.substr(0, level);
        if (level < 8) prefix.resize(8, '0');
        prefix.insert(8, "+");
        return {{"pluscode", code}, {"precision", std::min<std::size_t>(raw.size(), 11)}, {"max_precision", 11},
                {"status", id ? "matched" : "outside_coverage"}, {"admin", id ? admins.at(id) : Json(nullptr)},
                {"matched_length", level}, {"matched_pluscode", prefix},
                {"boundary_cell", bool(value & boundary)}, {"source_overlap", bool(value & overlap)},
                {"assignment", level < 11 ? "certified_full_cell" : "11_digit_center"}};
    }

    std::string lookup(const std::string& supplied) {
        const auto code = normalized(supplied);
        std::string raw = code;
        raw.erase(8, 1);
        std::lock_guard<std::mutex> guard(mutex);
        auto root = roots.find(raw.substr(0, 4));
        if (root == roots.end()) return leaf_result(code, raw, 4, 0).dump();
        if (root->second.owner >= 0) return leaf_result(code, raw, 4, static_cast<uint16_t>(root->second.owner)).dump();
        auto value = root->second.values[child_index(raw, 4)];
        if (value != branch) return leaf_result(code, raw, 6, value).dump();
        const auto bytes = tile(raw.substr(0, 6));
        std::size_t start = 0, end = bytes->size();
        for (int level = 6;; level = level == 10 ? 11 : level + 2) {
            Cursor cursor(*bytes, start, end);
            const auto tag = cursor.read(1);
            if (tag == 0) return leaf_result(code, raw, level, static_cast<uint16_t>(cursor.read(2))).dump();
            if (level >= static_cast<int>(std::min<std::size_t>(raw.size(), 11))) {
                Summary summary;
                Cursor subtree(*bytes, start, end);
                scan(subtree, level, admins, &summary);
                Json candidates = Json::array();
                for (auto id : summary.labels) if (id) candidates.push_back(admins.at(id));
                return Json{{"pluscode", code}, {"precision", std::min<std::size_t>(raw.size(), 11)}, {"max_precision", 11},
                    {"status", "ambiguous"}, {"admin", nullptr}, {"matched_length", nullptr},
                    {"boundary_cell", true}, {"source_overlap", bool(summary.flags & overlap)},
                    {"sampled_candidates", candidates}, {"includes_uncovered_samples", summary.labels.count(0) != 0},
                    {"candidate_semantics", "adaptive certified leaves and 11-digit centers; not exhaustive polygon intersections"}}.dump();
            }
            const auto values = vector_at(cursor, level == 10 ? 20 : 400);
            const auto selected = child_index(raw, level);
            if (values[selected] != branch)
                return leaf_result(code, raw, level == 10 ? 11 : level + 2, values[selected]).dump();
            for (int i = 0; i <= selected; ++i) if (values[i] == branch) {
                const auto length = cursor.read(4);
                start = cursor.pos;
                cursor.skip(length);
                end = cursor.pos;
            }
        }
    }
};

Engine::Engine(const std::string& path, Options options) {
    try { impl_ = std::make_unique<Impl>(path, options); }
    catch (const nlohmann::json::exception& error) { throw Error(PCAD_INVALID_INDEX, error.what()); }
}
Engine::~Engine() = default;
std::string Engine::lookup(const std::string& code) { return impl_->lookup(code); }
std::string Engine::lookup_latlng(double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180)
        throw Error(PCAD_INVALID_ARGUMENT, "Finite WGS84 latitude/longitude within world bounds required");
    return lookup(openlocationcode::Encode({latitude, longitude}, 11));
}
std::string Engine::metadata() const { return impl_->meta.dump(); }
std::string Engine::cache_stats() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    int pages = 0, peak = 0;
    sqlite3_db_status(impl_->db.value, SQLITE_DBSTATUS_CACHE_USED, &pages, &peak, 0);
    return Json{{"cached_tiles", impl_->cache.size()}, {"cached_bytes", impl_->cached_bytes},
        {"capacity_limit", impl_->options.cache_capacity}, {"byte_limit", impl_->options.cache_bytes},
        {"hits", impl_->hits}, {"misses", impl_->misses}, {"loads", impl_->loads},
        {"evictions", impl_->evictions}, {"compressed_bytes_read", impl_->compressed_bytes_read},
        {"sqlite_cache_bytes", pages}, {"root_count", impl_->roots.size()},
        {"admin_count", impl_->admins.size()}}.dump();
}
std::string Engine::prefetch_nearby(double latitude, double longitude, int radius_tiles, int max_tiles) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180 ||
        radius_tiles < 0 || radius_tiles > 20 || max_tiles < 0 || max_tiles > 1024)
        throw Error(PCAD_INVALID_ARGUMENT, "Invalid coordinates, radius (0..20), or max_tiles (0..1024)");
    std::lock_guard<std::mutex> guard(impl_->mutex);
    const int row = std::min(3599, static_cast<int>(std::floor((latitude + 90) * 20)));
    const int column = longitude == 180 ? 0 : static_cast<int>(std::floor((longitude + 180) * 20));
    std::vector<std::pair<int, std::string>> candidates;
    for (int dy = -radius_tiles; dy <= radius_tiles; ++dy) {
        if (row + dy < 0 || row + dy >= 3600) continue;
        for (int dx = -radius_tiles; dx <= radius_tiles; ++dx) {
            const int col = (column + dx + 7200) % 7200;
            const auto key = openlocationcode::Encode({-90 + (row + dy + 0.5) / 20, -180 + (col + 0.5) / 20}, 6).substr(0, 6);
            const auto root = impl_->roots.find(key.substr(0, 4));
            if (root != impl_->roots.end() && root->second.owner < 0 && root->second.values[child_index(key, 4)] == branch)
                candidates.emplace_back(dx * dx + dy * dy, key);
        }
    }
    std::sort(candidates.begin(), candidates.end());
    const auto available = candidates.size();
    const auto count = impl_->options.cache_bytes ? std::min({candidates.size(), static_cast<std::size_t>(max_tiles), impl_->options.cache_capacity}) : 0;
    candidates.resize(count);
    const auto previous_loads = impl_->loads;
    // Load farther cells first so the nearest cells remain most recent in the LRU.
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) impl_->tile(it->second);
    std::size_t retained = 0;
    for (const auto& item : candidates) if (impl_->cache.count(item.second)) ++retained;
    return Json{{"candidate_tiles", available}, {"visited_tiles", count},
        {"loaded_tiles", impl_->loads - previous_loads}, {"retained_tiles", retained},
        {"radius_tiles", radius_tiles}, {"cell_degrees", 0.05}}.dump();
}
void Engine::clear_cache() {
    std::lock_guard<std::mutex> guard(impl_->mutex);
    impl_->cache.clear(); impl_->recency.clear(); impl_->cached_bytes = 0;
    sqlite3_db_release_memory(impl_->db.value);
}
}
