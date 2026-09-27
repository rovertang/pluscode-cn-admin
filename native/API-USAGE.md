# 对外调用说明

## 通用约定

输入坐标为 WGS84，参数顺序为 `latitude, longitude`。纬度范围 [-90,90]，经度范围 [-180,180]，NaN/Infinity 拒绝。GCJ-02、BD-09 等坐标需要调用方先转换，库内不做坐标系猜测。

Plus Code 必须为包含 `+` 的完整码，至少 8 位有效字符；支持 8、10、11 位及更长合法码，计算最多使用前 11 位。输入最多 256 字节，短码和填充粗码不支持，ASCII 前后空白与小写会规范化。

数据库必须是完整 V2 SQLite，不能传 `.kv.jsonl.gz`、assets URI 或目录。实例打开期间保持文件内容不变；更新时先将新版本完整写入另一个文件并校验，关闭旧实例后再打开新文件。查询只读，不修改数据库。

## C++ / Linux

公开类 `pluscode_admin::Index` 是 C++17 RAII 封装，包含标准头文件即可，不需要调用方安装 nlohmann/json、SQLite 或 XZ。其返回的字符串为完整 UTF-8 JSON，解析器由调用方选择。

```cpp
#include <pluscode_admin/index.hpp>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        pluscode_admin::Index index(argv[1], {64, 8 * 1024 * 1024});
        std::cout << index.lookup_latlng(39.9042, 116.4074) << '\n';
        std::cout << index.lookup("8PFRWC34+MX2") << '\n';
        std::cout << index.cache_stats() << '\n';
        index.clear_cache();
    } catch (const pluscode_admin::Error& e) {
        std::cerr << e.status() << ": " << e.what() << '\n';
        return 1;
    }
}
```

编译和运行仓库中的等价示例，以下仍以 `native` 为工作目录：

```bash
SDK="$PWD/../sdk"
g++ -std=c++17 examples/query.cpp -I"$SDK/include" \
  -L"$SDK/linux-x86_64/lib" -lpluscode_admin \
  -Wl,-rpath,"$SDK/linux-x86_64/lib" -o "$WORK/query"
"$WORK/query" ../data/processed/v2/pluscode_admin_v2.sqlite 39.9042 116.4074
```

C++ 方法：

| 方法 | 返回/用途 |
| --- | --- |
| `Index(database, Options{capacity, bytes})` | 打开实例；不读取全部混合块 |
| `lookup(code)` | 查询结果 JSON |
| `lookup_latlng(latitude, longitude)` | 查询结果 JSON |
| `metadata()` | 数据库元数据 JSON |
| `cache_stats()` | 内存缓存和累计加载统计 JSON |
| `prefetch_nearby(lat, lng, radius_tiles=1, max_tiles=16)` | 周边预热报告 JSON |
| `clear_cache()` | 清除解压块缓存并请求 SQLite 释放页缓存 |
| 析构 | 关闭原生句柄；实例不可复制或移动 |

默认 `Options` 为 64 块/8 MiB。容量允许 0..65536，字节数允许 0..4 GiB（受平台 size_t 范围限制），任一为 0 禁用块缓存。预热参数说明见 [MEMORY.md](MEMORY.md)。方法可由多个线程共享调用，实例内部串行执行；C++ 对象析构前必须停止外部线程访问该对象。

C++ 封装内部只把 UTF-8 字节、数值和句柄传给 C ABI，不跨 SO 传递 STL 对象或 C++ 异常。这样调用方可使用 GCC/libstdc++，而交付库使用内部静态 libc++。`Error` 在调用方封装内构造。若与供应商预编译库一同加载，仍应按项目正常进行 ABI/系统版本集成测试。

## C ABI

头文件 `pluscode_admin/pluscode_admin.h` 可用于 C、C++、其他 FFI 或系统组件。所有函数返回 `pcad_status`，C ABI 不向外抛 C++ 异常；`pcad_version()` 返回版本，`pcad_abi_version()` 返回 1。

```c
#include <pluscode_admin/pluscode_admin.h>
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    pcad_handle handle = 0;
    pcad_options options = {sizeof(pcad_options), 64, 8ULL * 1024 * 1024};
    char *json = NULL, *error = NULL;
    pcad_status status = pcad_open(argv[1], &options, &handle, &error);
    if (status == PCAD_OK) {
        status = pcad_lookup_latlng(handle, 39.9042, 116.4074, &json, &error);
        if (status == PCAD_OK) puts(json);
    }
    if (status != PCAD_OK) fprintf(stderr, "%s\n", error ? error : "native error");
    pcad_free(json);
    pcad_free(error);
    if (handle) pcad_close(handle, NULL);
    return status == PCAD_OK ? 0 : 1;
}
```

`pcad_open` 的 `options=NULL` 选择默认值。结果和错误都是库分配的 UTF-8 NUL 结尾字符串，必须分别调用 `pcad_free()`，不能 `delete[]` 或交给其他运行时释放。`error` 输出参数可以为 NULL，结果输出不能为 NULL，结果和错误输出地址不能相同。复用输出变量前先释放上次返回的内容。

`pcad_metadata`、`pcad_cache_stats`、`pcad_prefetch_nearby` 同样返回 JSON；签名见头文件。句柄注册表可安全处理查询和关闭竞争：已经取得实例的查询可以完成；关闭后新查询返回 `PCAD_CLOSED`。重复 `pcad_close` 返回 `PCAD_CLOSED`。

| 状态码 | 含义 |
| --- | --- |
| `PCAD_OK=0` | 成功；查询结果本身仍可能为未覆盖/歧义 |
| `PCAD_INVALID_ARGUMENT=1` | 坐标、码、参数或输出指针无效 |
| `PCAD_INVALID_INDEX=2` | V2 格式/完整性无效、缺块、损坏压缩或树结构 |
| `PCAD_IO_ERROR=3` | 无法打开数据库，如不存在或无权限 |
| `PCAD_CLOSED=4` | 无效或已关闭句柄 |
| `PCAD_INTERNAL_ERROR=5` | 其他内部异常，例如分配失败 |

查询期间 SQLite 读取错误统一报告为索引读取失败 `PCAD_INVALID_INDEX`，具体原因在错误消息中。启动验证元数据、目录和字典；块在首次读取时验证，所以“打开成功”不意味着已扫描全部文件。

## Android 原生 C++

将选定 ABI 的库放在项目预编译目录，例如 `prebuilt/arm64-v8a/libpluscode_admin.so`，并包含 SDK 的 `include/`：

```cmake
add_library(pluscode_admin SHARED IMPORTED)
set_target_properties(pluscode_admin PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt/${ANDROID_ABI}/libpluscode_admin.so")
target_include_directories(your_native_target PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(your_native_target PRIVATE pluscode_admin)
```

调用方式与 Linux C++ 相同。交付 SO 内部静态链接 libc++，无需另加该库的 `libc++_shared.so`。如集成到 AOSP/vendor/system 组件，需按照该组件的 Android 构建规则安装库和设置文件读取权限；NDK 产物适用于公开 native API，不能保证任意系统分区 linker namespace 配置自动允许加载。需要平台编译时可复用源码并关闭 `PCAD_JNI`。

## Android APP / Java

目录示例：

```text
app/libs/pluscode-admin-1.0.0.jar
app/src/main/jniLibs/arm64-v8a/libpluscode_admin.so
app/src/main/jniLibs/x86_64/libpluscode_admin.so
app/src/main/jniLibs/armeabi-v7a/libpluscode_admin.so
app/src/main/assets/pluscode_admin_v2.sqlite
```

可按设备减少 ABI。`build.gradle.kts`：

```kotlin
android {
    defaultConfig { minSdk = 21 }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
}
dependencies { implementation(files("libs/pluscode-admin-1.0.0.jar")) }
```

不要既通过 `jniLibs` 又通过另一 native 构建重复打包相同库。JAR 内含 `META-INF/proguard/pluscode-admin.pro` 保留 JNI 名称；同时提供 `consumer-rules.pro`，若工程自定义压缩流程忽略 JAR 规则，可将其加入 release 的 `proguardFiles`。类名、native 方法名和嵌套类型构造器不能被改名或删除。

库只接收真实文件路径。首次运行在后台以流方式将 asset 复制到 `context.getFilesDir()`（或由应用下载/部署到私有目录）；不要将 220 MB 文件一次读成 byte[]。见可直接使用的 [AndroidDatabase.java](examples/AndroidDatabase.java)，它使用临时文件和完成后的改名，示例文件名包含版本。升级数据库需使用新版本名并更新校验哈希，避免旧文件被误用。

```java
import com.askcodex.pluscode.PlusCodeIndex;

// 在后台线程执行。databasePath 是准备完成的私有目录绝对路径。
try (PlusCodeIndex index = new PlusCodeIndex(databasePath, 64, 8L * 1024 * 1024)) {
    PlusCodeIndex.Result result = index.lookupLatLng(39.9042, 116.4074);
    if (result.isMatched()) {
        PlusCodeIndex.Admin admin = result.admin;
        String province = admin.province;
        String city = admin.city;       // 可能为 null
        String county = admin.county;   // 可能为 null
        String countyCode = admin.countyCode;
    } else if (result.isAmbiguous()) {
        // 粗码跨多个子格；要求更细输入，或向业务层返回 sampledCandidates。
    } else {
        // outside_coverage：本数据集没有覆盖。
    }
}
```

以上 `try` 适合单次任务。持续定位应在服务/仓储组件内持有一个实例，在停止定位并停止调用后 `close()`。所有 Java 公共实例方法同步，查询、预热和关闭不会竞争使用已释放句柄。Java `close()` 可重复调用；忘记关闭不会自动由 finalizer 兜底。

Java API：`lookup`、`lookupLatLng` 返回类型化 `Result`，`metadataJson`、`cacheStatsJson`、`prefetchNearby` 返回 JSON 字符串，`clearCache` 清缓存。无效输入抛 `IllegalArgumentException`，关闭后调用抛 `IllegalStateException`，I/O、索引损坏和一般原生失败抛 `IOException`。SO 缺失/架构不符属于类加载时的 `UnsatisfiedLinkError`。

## Kotlin

```kotlin
import com.askcodex.pluscode.PlusCodeIndex

PlusCodeIndex(databasePath, 64, 8L * 1024 * 1024).use { index ->
    val result = index.lookupLatLng(39.9042, 116.4074)
    val admin = result.admin
    if (result.isMatched && admin != null) {
        println("${admin.province} / ${admin.city ?: ""} / ${admin.county ?: ""}")
    }
    println(index.cacheStatsJson())
}
```

协程项目可在 `Dispatchers.IO` 使用；库自身没有协程依赖，也不内置任务线程。Java 字段在 Kotlin 中为平台类型，按上例处理可空结果及字段。

## 标准 Linux JVM

同一个 JAR 可直接用于普通 Java 程序：

```bash
javac -cp ../sdk/java/pluscode-admin-1.0.0.jar -d "$WORK/classes" examples/JavaExample.java
java -Djava.library.path="$PWD/../sdk/linux-x86_64/lib" \
  -cp "../sdk/java/pluscode-admin-1.0.0.jar:$WORK/classes" JavaExample \
  ../data/processed/v2/pluscode_admin_v2.sqlite
```

通过 `System.loadLibrary("pluscode_admin")` 自动加载平台库。没有提供 Windows DLL 或 macOS dylib；这里的“标准 JAR”指无 Android 专用 Java API 的类库，运行仍需要匹配宿主平台的 JNI 动态库。

## 结果字段

| JSON / Java 字段 | 含义 |
| --- | --- |
| `status` | `matched`、`outside_coverage`、`ambiguous` |
| `admin` | 命中的 Admin；未覆盖和歧义时为 null |
| `province/city/county` | 名称，缺失层级保持 null |
| `province_code/city_code/county_code` / `provinceCode/cityCode/countyCode` | 编码字符串，保留前导零；缺失保持 null |
| `precision` / `maxPrecision` | 本次使用精度 / 最大支持精度 11 |
| `matched_length` / `matchedLength` | 实际叶节点层级 4/6/8/10/11；歧义 JSON 为 null，Java 为 -1 |
| `matched_pluscode` / `matchedPlusCode` | 命中的叶格；歧义时没有，Java 为 null |
| `boundary_cell` / `boundaryCell` | 边界提示；粗码歧义也为 true |
| `source_overlap` / `sourceOverlap` | 原始行政区覆盖存在重叠的提示 |
| `assignment` | `certified_full_cell` 或 `11_digit_center`；歧义时缺失 |
| `sampled_candidates` / `sampledCandidates` | 粗码歧义采样候选；Java 在非歧义时为空列表 |
| `includes_uncovered_samples` / `includesUncoveredSamples` | 粗码包含无覆盖采样；Java 默认 false |
| `Result.json` / `Admin.json` | 完整 UTF-8 语义的 Java 字符串，保留原始来源和扩展字段 |

直辖市在源规则中可把省级名称重复为市级。港澳或其他缺层级记录不会补造县名。最细边界按 11 位格中心采样，不保证格内任意原始点都属于该区域；`sampledCandidates` 也不是精确穷举所有与格相交的多边形。位置恰好靠近边界时，由业务决定提示、延迟切换或融合轨迹，库不做行政区切换防抖。

## Android 页面大小

产物检查 64 位 ELF 的 LOAD 段至少 16 KiB 对齐。APP 打包也需满足 ZIP 对齐要求，建议 AGP 8.5.1+，并在目标设备上测试最终 APK；仅验证 SO 不等于验证整个 APK。参考 [Android 官方页面大小文档](https://developer.android.com/guide/practices/page-sizes)。
