# PlusCode Admin Native SDK 1.0.0

离线查询 WGS84 经纬度或完整 Plus Code 对应的省、市、县，直接读取已生成的 V2 SQLite。提供 C++17 接口、稳定 C ABI、JNI，以及 Java 8 字节码的标准 JAR，Java/Kotlin 共用同一套类。查询运行时不需要 Python、GIS 库或网络。

## 交付入口

| 成果 | 相对本目录的路径 |
| --- | --- |
| Linux x86_64 动态库 | `../sdk/linux-x86_64/lib/libpluscode_admin.so` |
| Linux 命令行示例 | `../sdk/linux-x86_64/bin/pcad-query` |
| Android 动态库 | `../sdk/android/jniLibs/{arm64-v8a,x86_64,armeabi-v7a}/libpluscode_admin.so` |
| 标准 JAR | `../sdk/java/pluscode-admin-1.0.0.jar` |
| Java 源码 JAR | `../sdk/java/pluscode-admin-1.0.0-sources.jar` |
| C/C++ 头文件 | `include/pluscode_admin/`，SDK 中也有副本 |
| 全国数据库 | `../data/processed/v2/pluscode_admin_v2.sqlite`，220,561,408 字节 |
| 源码、二进制、数据库完整包 | `../releases/pluscode-admin-1.0.0-complete.zip`，按需生成 |
| 调用说明 | [API-USAGE.md](API-USAGE.md) |
| 内存与动态加载 | [MEMORY.md](MEMORY.md) |
| 验证结果与限制 | [VALIDATION.md](VALIDATION.md) |

**Android APP 需要 JAR + 对应 ABI 的 SO + 数据库。** JAR 是标准 Java 类库，JNI 的机器码仍由 SO 提供；不把 SO 或地图塞进 JAR。Linux JVM 使用同一个 JAR 和 Linux SO。Linux SO 和 Android SO 不可互换。

Android 库以 NDK r29、API 21、静态 libc++ 构建；64 位库按 16 KiB ELF 页面对齐。SQLite、liblzma、zlib、OLC 已静态链接进各 SO。Linux 便携版使用 Zig 0.14.1，目标为 x86_64 / glibc 2.28；实际符号依赖与测试环境见验证报告。Linux ARM64 或 Android 系统分区中的特定平台构建，可用本工程重新编译。

## 最小试用

以下命令在 Linux / WSL 中执行，工作目录为本 README 所在的 `native`：

```bash
../sdk/linux-x86_64/bin/pcad-query ../data/processed/v2/pluscode_admin_v2.sqlite 39.9042 116.4074
```

返回东城区，包含省市县名称、编码、实际命中层级与边界标记。坐标顺序是纬度、经度。

## 内存行为

启动只读取 3,213 条顶层记录和行政区字典，**不会读取全部 92,174 个混合块**。普通查询按 6 位前缀从 SQLite 读取一个混合块并解压，LRU 默认最多 64 块、8 MiB。任意位置查询同样按需读取；车辆移动只会逐步替换旧块。

可调用 `prefetch_nearby` / `prefetchNearby` 提前读取周边块，并通过 `cache_stats` / `cacheStatsJson` 观察命中、加载次数与缓存字节数。8 MiB 是块缓存限额，不是整个进程或 JNI 的总内存限制。详细取舍及约 110 km 层级的对应关系见 [MEMORY.md](MEMORY.md)。

本机独立 C++ 测试进程打开全国数据库后 RSS 为 14,400 KiB，测试场景最高记录为 16,480 KiB；启动时加载混合块数为 0。全国 92,174 个块、105,039 次查询对照全部通过。Android 产物已在 Android 13 arm64-v8a 设备运行，Demo 的 14 项 JNI 自检和界面流程均通过；详见 [VALIDATION.md](VALIDATION.md) 和 `../validation/android/`。

## 从源码构建

工程使用 CMake 3.26+，C/C++ 编译器、make/ninja，JNI 构建需 JDK 11+，Android 需 NDK。依赖的原始源码包和 SHA-256 已保存在 `source/`，CMake 离线解包并逐包校验，无需构建时下载。

```bash
export JAVA_HOME=/absolute/path/to/jdk
export CMAKE_BIN=/absolute/path/to/cmake
# Windows 的本项目通过 WSL 使用 C:/AI/askAI/tmp/askCodex/ 下的路径。
# 在其他 Linux 机器上指定该机器专用的、源码树外的临时目录。
WORK=/absolute/path/to/temporary-build
bash scripts/build.sh linux "$WORK"
bash scripts/build.sh java "$WORK"

export ANDROID_NDK_HOME=/absolute/path/to/android-ndk-r29
bash scripts/build.sh android "$WORK"
```

Linux 默认使用系统编译器，其 glibc 兼容范围由系统决定。复现交付的便携目标时，在首次配置空构建目录前设置 `PCAD_ZIG=/absolute/path/to/zig-0.14.1/zig`。`PCAD_JOBS` 控制编译并行度；`PCAD_ANDROID_ABIS='arm64-v8a x86_64'` 可筛选 ABI。`build.sh all "$WORK"` 构建全部；第三个参数可指定独立 SDK 输出目录。

底层 CMake 示例：

```bash
cmake -S . -B "$WORK/custom" -DCMAKE_BUILD_TYPE=Release -DPCAD_JNI=OFF -DPCAD_TESTS=OFF
cmake --build "$WORK/custom" -j4
cmake --install "$WORK/custom" --prefix /absolute/path/to/sdk
```

`PCAD_JNI=OFF` 适合仅 C/C++ 接入，可省去 JDK 需求。普通构建默认带 JNI，C++ 客户端使用时不需要 JVM。NDK ZIP 应在 Linux 下保留符号链接解压，不能把链接还原成普通文本文件。

## 验证与复现

```bash
# 仅验证脚本需要 Python 3.11+ 与 openlocationcode==1.0.1。
# 使用独立 venv 安装 tests/reference/requirements-runtime.txt。
export PYTHON_BIN=/absolute/path/to/venv/bin/python
export KOTLINC=/absolute/path/to/kotlinc  # 可选 Kotlin 编译验证
bash scripts/validate.sh "$WORK"
```

测试包含 C++/C ABI、缓存行为、并发访问、全国每块抽取查询与 Python 的逐字段对照、损坏数据、JNI UTF-16/UTF-8、Java 生命周期、Kotlin，以及外部 GCC 客户端。报告写入 `../validation/native/`。`scripts/package_release.py` 生成校验清单与完整 ZIP；先确保 SDK 和验证报告完整。

## 源码布局

```text
include/       对外 C ABI 与仅依赖标准库的 C++ RAII 封装
src/           SQLite/XZ 查询引擎、C ABI 句柄管理、JNI
java/          Java/Kotlin 公共类与类型化查询结果
examples/      可编译 C++、Java、Kotlin 示例
tests/         功能、差分与内存验证
scripts/       离线依赖校验、构建、验证、打包
cmake/         固定依赖构建与便携 Linux 工具链
source/        依赖原始源码压缩包与版本锁定
../sdk/       最终 SDK
../validation/native/  验证报告
../releases/  按需生成的完整交付包
```

数据沿用 V2，最细为 11 位格中心归属；边界附近不等价于对输入点做精确多边形运算。省市县可能有缺失字段，粗码可能返回多个候选。接入时必须处理这些状态，具体约定见调用文档。第三方源码及运行时声明见 `../sdk/THIRD_PARTY_NOTICES.txt`。
