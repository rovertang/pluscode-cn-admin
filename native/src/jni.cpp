#include "pluscode_admin/pluscode_admin.h"
#include <jni.h>
#include <nlohmann/json.hpp>
#include <codecvt>
#include <locale>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Json = nlohmann::json;
using Text = std::unique_ptr<char, decltype(&pcad_free)>;
constexpr char admin_class[] = "com/askcodex/pluscode/PlusCodeIndex$Admin";
constexpr char result_class[] = "com/askcodex/pluscode/PlusCodeIndex$Result";
std::string utf8(JNIEnv* env, jstring text) {
    if (!text) throw std::invalid_argument("String must not be null");
    const auto size = env->GetStringLength(text);
    const jchar* chars = env->GetStringChars(text, nullptr);
    if (!chars) throw std::bad_alloc();
    struct Release { JNIEnv* env; jstring text; const jchar* chars; ~Release() { env->ReleaseStringChars(text, chars); } } release{env, text, chars};
    std::u16string copy;
    copy.reserve(size);
    for (jsize i = 0; i < size; ++i) copy.push_back(static_cast<char16_t>(chars[i]));
    try { return std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(copy); }
    catch (const std::range_error&) { throw std::invalid_argument("Invalid UTF-16 string"); }
}
jstring java_string(JNIEnv* env, const std::string& text) {
    const auto chars = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(text);
    std::vector<jchar> copy(chars.begin(), chars.end());
    return env->NewString(copy.data(), static_cast<jsize>(copy.size()));
}
void throw_java(JNIEnv* env, pcad_status status, const std::string& message) {
    if (env->ExceptionCheck()) return;
    const char* name = status == PCAD_INVALID_ARGUMENT ? "java/lang/IllegalArgumentException" :
                       status == PCAD_CLOSED ? "java/lang/IllegalStateException" : "java/io/IOException";
    jclass type = env->FindClass(name);
    if (!type) return;
    auto constructor = env->GetMethodID(type, "<init>", "(Ljava/lang/String;)V");
    jstring text = java_string(env, message);
    if (constructor && text) {
        jobject exception = env->NewObject(type, constructor, text);
        if (exception) { env->Throw(static_cast<jthrowable>(exception)); env->DeleteLocalRef(exception); }
    }
    if (text) env->DeleteLocalRef(text);
    env->DeleteLocalRef(type);
}
bool check(JNIEnv* env, pcad_status status, char* message) {
    Text owned(message, pcad_free);
    if (status == PCAD_OK) return true;
    throw_java(env, status, owned ? owned.get() : "Native operation failed");
    return false;
}
template<class Function> jobject protect(JNIEnv* env, Function function) noexcept {
    try { return function(); }
    catch (const std::invalid_argument& error) { try { throw_java(env, PCAD_INVALID_ARGUMENT, error.what()); } catch (...) {} }
    catch (const std::exception& error) { try { throw_java(env, PCAD_INTERNAL_ERROR, error.what()); } catch (...) {} }
    catch (...) {}
    if (!env->ExceptionCheck()) {
        auto type = env->FindClass("java/lang/OutOfMemoryError");
        if (type) { env->ThrowNew(type, "Native result allocation failed"); env->DeleteLocalRef(type); }
    }
    return nullptr;
}
jobjectArray strings(JNIEnv* env, const Json& object, std::initializer_list<const char*> keys) {
    jclass type = env->FindClass("java/lang/String");
    if (!type) return nullptr;
    jobjectArray result = env->NewObjectArray(static_cast<jsize>(keys.size()), type, nullptr);
    env->DeleteLocalRef(type);
    if (!result) return nullptr;
    jsize i = 0;
    for (auto key : keys) {
        if (object.contains(key) && !object.at(key).is_null()) {
            jstring text = java_string(env, object.at(key).get<std::string>());
            if (!text) return nullptr;
            env->SetObjectArrayElement(result, i, text);
            env->DeleteLocalRef(text);
            if (env->ExceptionCheck()) return nullptr;
        }
        ++i;
    }
    return result;
}
jobject admin(JNIEnv* env, const Json& item) {
    if (item.is_null()) return nullptr;
    if (env->PushLocalFrame(16) < 0) return nullptr;
    jobject result = nullptr;
    try {
        auto type = env->FindClass(admin_class);
        auto constructor = type ? env->GetMethodID(type, "<init>", "(ILjava/lang/String;[Ljava/lang/String;)V") : nullptr;
        if (!constructor) { env->PopLocalFrame(nullptr); return nullptr; }
        auto fields = strings(env, item, {"province", "province_code", "city", "city_code", "county", "county_code"});
        auto json = java_string(env, item.dump());
        if (constructor && fields && json && !env->ExceptionCheck())
            result = env->NewObject(type, constructor, static_cast<jint>(item.at("admin_id").get<int>()), json, fields);
    } catch (...) { env->PopLocalFrame(nullptr); throw; }
    return env->PopLocalFrame(result);
}
jobject response(JNIEnv* env, char* data) {
    Text owned(data, pcad_free);
    const auto json = Json::parse(owned.get());
    if (env->PushLocalFrame(24) < 0) return nullptr;
    jobject result = nullptr;
    try {
        auto type = env->FindClass(result_class);
        auto constructor = type ? env->GetMethodID(type, "<init>", "(Ljava/lang/String;[Ljava/lang/String;[I[ZLcom/askcodex/pluscode/PlusCodeIndex$Admin;[Lcom/askcodex/pluscode/PlusCodeIndex$Admin;)V") : nullptr;
        if (!constructor) { env->PopLocalFrame(nullptr); return nullptr; }
        auto text = strings(env, json, {"pluscode", "status", "matched_pluscode", "assignment"});
        auto raw = java_string(env, owned.get());
        auto chosen = admin(env, json.at("admin"));
        if (env->ExceptionCheck()) { env->PopLocalFrame(nullptr); return nullptr; }
        const auto candidate_values = json.value("sampled_candidates", Json::array());
        auto admin_type = env->FindClass(admin_class);
        auto candidates = admin_type ? env->NewObjectArray(static_cast<jsize>(candidate_values.size()), admin_type, nullptr) : nullptr;
        if (!candidates || !raw || !text || env->ExceptionCheck()) { env->PopLocalFrame(nullptr); return nullptr; }
        for (std::size_t i = 0; i < candidate_values.size(); ++i) {
            auto item = admin(env, candidate_values[i]);
            if (env->ExceptionCheck()) { env->PopLocalFrame(nullptr); return nullptr; }
            env->SetObjectArrayElement(candidates, static_cast<jsize>(i), item);
            env->DeleteLocalRef(item);
        }
        jint numbers[] = {json.at("precision").get<int>(), 11, json.at("matched_length").is_null() ? -1 : json.at("matched_length").get<int>()};
        jboolean flags[] = {static_cast<jboolean>(json.at("boundary_cell").get<bool>()),
                           static_cast<jboolean>(json.at("source_overlap").get<bool>()),
                           static_cast<jboolean>(json.value("includes_uncovered_samples", false))};
        auto nums = env->NewIntArray(3);
        auto bits = env->NewBooleanArray(3);
        if (nums && bits) {
            env->SetIntArrayRegion(nums, 0, 3, numbers);
            env->SetBooleanArrayRegion(bits, 0, 3, flags);
            if (!env->ExceptionCheck()) result = env->NewObject(type, constructor, raw, text, nums, bits, chosen, candidates);
        }
    } catch (...) { env->PopLocalFrame(nullptr); throw; }
    return env->PopLocalFrame(result);
}
}

extern "C" JNIEXPORT jlong JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeOpen(JNIEnv* env, jclass, jstring path, jint capacity, jlong bytes) {
    pcad_handle handle = 0;
    protect(env, [&]() -> jobject {
        if (capacity < 0 || bytes < 0) throw std::invalid_argument("Cache limits must be non-negative");
        const auto name = utf8(env, path);
        if (name.find('\0') != std::string::npos) throw std::invalid_argument("NUL in database path");
        pcad_options options{sizeof(pcad_options), static_cast<uint32_t>(capacity), static_cast<uint64_t>(bytes)};
        char* error = nullptr;
        auto status = pcad_open(name.c_str(), &options, &handle, &error);
        check(env, status, error);
        return nullptr;
    });
    return static_cast<jlong>(handle);
}
extern "C" JNIEXPORT jobject JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeLookupCode(JNIEnv* env, jclass, jlong handle, jstring code) {
    return protect(env, [&]() -> jobject {
        auto text = utf8(env, code);
        if (text.find('\0') != std::string::npos) throw std::invalid_argument("NUL in Plus Code");
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_lookup_code(static_cast<pcad_handle>(handle), text.c_str(), &data, &error);
        if (!check(env, status, error)) return nullptr;
        return response(env, data);
    });
}
extern "C" JNIEXPORT jobject JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeLookupLatLng(JNIEnv* env, jclass, jlong handle, jdouble latitude, jdouble longitude) {
    return protect(env, [&]() -> jobject {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_lookup_latlng(static_cast<pcad_handle>(handle), latitude, longitude, &data, &error);
        if (!check(env, status, error)) return nullptr;
        return response(env, data);
    });
}
extern "C" JNIEXPORT jstring JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeMetadata(JNIEnv* env, jclass, jlong handle) {
    return static_cast<jstring>(protect(env, [&]() -> jobject {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_metadata(static_cast<pcad_handle>(handle), &data, &error);
        Text owned(data, pcad_free);
        if (!check(env, status, error)) return nullptr;
        return java_string(env, owned.get());
    }));
}
extern "C" JNIEXPORT void JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeClearCache(JNIEnv* env, jclass, jlong handle) {
    protect(env, [&]() -> jobject { char* error = nullptr; auto status = pcad_clear_cache(handle, &error); check(env, status, error); return nullptr; });
}
extern "C" JNIEXPORT jstring JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeCacheStats(JNIEnv* env, jclass, jlong handle) {
    return static_cast<jstring>(protect(env, [&]() -> jobject {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_cache_stats(handle, &data, &error);
        Text owned(data, pcad_free);
        if (!check(env, status, error)) return nullptr;
        return java_string(env, owned.get());
    }));
}
extern "C" JNIEXPORT jstring JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativePrefetchNearby(JNIEnv* env, jclass, jlong handle, jdouble latitude, jdouble longitude, jint radius, jint maximum) {
    return static_cast<jstring>(protect(env, [&]() -> jobject {
        char* data = nullptr; char* error = nullptr;
        auto status = pcad_prefetch_nearby(handle, latitude, longitude, radius, maximum, &data, &error);
        Text owned(data, pcad_free);
        if (!check(env, status, error)) return nullptr;
        return java_string(env, owned.get());
    }));
}
extern "C" JNIEXPORT void JNICALL Java_com_askcodex_pluscode_PlusCodeIndex_nativeClose(JNIEnv* env, jclass, jlong handle) {
    protect(env, [&]() -> jobject { char* error = nullptr; auto status = pcad_close(handle, &error); check(env, status, error); return nullptr; });
}
