# pluscode-cn-admin

中国省、市、县与 Plus Code 的离线查询代码及预编译 SDK。输入 WGS84 经纬度或完整 Plus Code，查询 2024 年初行政区数据中的省、市、县名称和代码。坐标参数顺序是 `latitude, longitude`；GCJ-02、BD-09 坐标需要调用方先转换。

本仓库保存 Android Demo 工程、C++/C/Java/JNI 原生实现与 Linux/Android SDK，以及本地 Web 服务代码。**全国 V2 数据库、Redis KV 导出和 APK 是 Release 成果物，不提交到代码仓库。仅克隆代码无法运行需要数据的查询。** 原始 SHP 下载及数据制作流程不在本仓库中。数据网格最细为 11 位 Plus Code；边界附近的格中心归属不能替代原始行政边界的精确测绘，调用方应检查 `boundary_cell` 和 `status`。

## 目录

| 路径 | 内容 |
| --- | --- |
| `android-demo/project/` | Android Studio 工程；依赖根目录 `sdk/` 和下载后的 V2 SQLite |
| `android-demo/scripts/` | 构建、安装及设备验证脚本 |
| `native/` | C++17 查询引擎、C ABI、JNI、测试、构建脚本和固定版本的第三方源码包 |
| `sdk/linux-x86_64/` | Linux x86_64 SO、头文件与 `pcad-query` |
| `sdk/android/`、`sdk/java/` | Android 三种 ABI 的 SO 和 Java JAR |
| `web/api/`、`web/tools/` | Node.js/TypeScript API、地图页面和 Redis 导入工具 |
| `native/tests/reference/` | 原生差分测试使用的 V2 纯查询参考实现 |
| `docs/assets/`、`validation/` | Android 截图及已有交付物的验证记录 |

## V2 数据包

从本项目的 [GitHub Releases](https://github.com/rovertang/pluscode-cn-admin/releases) 下载以下文件。代码仓库没有数据库；如果 Release 页面暂未显示这些文件，需等资产上传后再配置需要数据的功能。两个数据包均只包含 **V2** 数据，不含原始 Shapefile 或 V1 数据。

| Release 文件 | ZIP 内文件 | 用途 |
| --- | --- | --- |
| `pluscode-admin-v2-sqlite.zip` | `pluscode_admin_v2.sqlite` | Android 构建、Linux SDK 和 Python 本地查询 |
| `pluscode-admin-v2-redis-kv.zip` | `pluscode_admin_v2.kv.jsonl.gz` | 导入 Redis，供 Web API 查询；不能直接作为 SQLite 数据库打开 |
| `pluscode-demo-debug.apk` | 无需解压 | 已内置 V2 SQLite 的 Android Demo 安装包 |

把两个 ZIP 的内容分别解压到**克隆后的仓库根目录**的 `data/processed/v2/`；ZIP 文件本身可留在下载目录。以下示例假定文件下载到了当前用户的 `Downloads`，请按实际位置修改 `$downloadDir`，并从仓库根目录执行：

```powershell
$downloadDir = Join-Path $HOME 'Downloads'
Expand-Archive (Join-Path $downloadDir 'pluscode-admin-v2-sqlite.zip') -DestinationPath .\data\processed\v2 -Force
Expand-Archive (Join-Path $downloadDir 'pluscode-admin-v2-redis-kv.zip') -DestinationPath .\data\processed\v2 -Force
```

只使用 Android/Linux/Python 时仅需 SQLite 包；只运行 Web 服务时仅需 Redis KV 包。直接安装 APK 时不需要另行下载 SQLite 包。SQLite 原文件大小为 220,561,408 字节，SHA-256 为 `9abaed308d8d1286fd08fe6ab1517d5345198892a5873dfeb2dbd34b0687fe4f`；Redis KV 原文件 SHA-256 为 `c2459b67b8d04c56fa589d7a470702da343a8ced16d535bb7dbe20c3d1aaf87f`。Redis 包解压后仍是 `.jsonl.gz`，导入脚本会自行解 gzip，不要手动展开成 JSONL。

下载后的三个 Release 文件可用 `Get-FileHash <文件路径> -Algorithm SHA256` 核验：

| 文件 | SHA-256 |
| --- | --- |
| `pluscode-admin-v2-sqlite.zip` | `caf0abbb1b76dc2ea58fede610bbc8571c41d66ffb3f583968a5cfa6d2cc6e1b` |
| `pluscode-admin-v2-redis-kv.zip` | `dc1f79f6c993e14bcc9af01fd8d6a084a87a67883ef97c5f00545855073adbfa` |
| `pluscode-demo-debug.apk` | `1052109cdf992995cf0e56868769c3c8986d2e6f22643b5bc595c5459b2f4810` |

## Linux SDK

在 Linux x86_64 系统中，从仓库根目录运行：

```bash
sdk/linux-x86_64/bin/pcad-query data/processed/v2/pluscode_admin_v2.sqlite 39.9042 116.4074
```

Linux SO、C/C++ 头文件和示例位于 `sdk/linux-x86_64/`、`sdk/include/` 与 `native/examples/`。C、C++、Java/Kotlin 接口及 Linux 兼容条件见 [`native/API-USAGE.md`](native/API-USAGE.md) 和 [`native/README.md`](native/README.md)。原生运行时按需读取压缩块，不把整个数据库解压到内存。

Python 参考查询使用 `native/tests/reference/query_v2.py`，依赖见同目录的 `requirements-runtime.txt`。安装依赖后，可从仓库根目录执行 `python native/tests/reference/query_v2.py --latlng 39.9042 116.4074`，默认读取上述 SQLite 路径。

## Android Demo

下载 SQLite 包后，在 Android Studio 中打开 `android-demo/project/`。需要 JDK 17+、Android SDK Platform 35 和 Build Tools 35.0.0。工程从 `sdk/` 引用 JAR 和三种 ABI 的 SO，并在构建时将 SQLite 复制进 APK；代码仓库不包含数据库副本或 APK。Linux/WSL 命令行构建：

```bash
export JAVA_HOME=/absolute/path/to/jdk
export ANDROID_HOME=/absolute/path/to/android-sdk
bash android-demo/scripts/build.sh :app:assembleDebug
```

预构建的 `pluscode-demo-debug.apk` 作为单独 Release 资产提供。自行构建使用本机调试签名，可能无法覆盖安装该 Release APK。项目说明见 [`android-demo/project/README.md`](android-demo/project/README.md)。

## Web API 与地图页面

先下载并解压 Redis KV 包，再准备一个专用 Redis 数据库。导入脚本会写入同名键，不应指向存放其他业务数据的 Redis DB。以下命令从仓库根目录的 PowerShell 执行：

```powershell
$env:REDIS_URL = 'redis://127.0.0.1:6379/0'
Set-Location web/tools
npm.cmd ci
node import_redis.js
node verify_redis.js

Set-Location ../api
npm.cmd ci
npm.cmd run typecheck
npm.cmd test
npm.cmd start
```

启动后打开 `http://localhost:3000/`，或请求 `GET /api/query?lat=39.9042&lng=116.4074`。服务启动时验证 V2 元数据，查询时按需从 Redis 读取压缩块。`REDIS_URL` 可指定其他实例，凭据只通过环境变量提供。地图页面使用在线 Leaflet、OpenStreetMap 瓦片和 Open Location Code CDN，因此页面本身需要网络；SDK 查询不需要网络。Web 端尚无生产环境性能与端到端验收记录。

## 来源与许可

行政区源数据来自 [CTAmap / shengshixian.com](https://github.com/ruiduobao/shengshixian.com)，数据版本为 2024 年初。仓库源码按 [MIT 许可证](LICENSE) 发布；第三方组件声明见 [`sdk/THIRD_PARTY_NOTICES.txt`](sdk/THIRD_PARTY_NOTICES.txt)。原始行政区数据与第三方依赖保留各自许可及引用要求。
