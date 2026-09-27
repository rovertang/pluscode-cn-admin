# PlusCodeDemo Android Studio 工程

本工程通过仓库根部的 `sdk/` 引用 Java/JNI SDK，并从 `data/processed/v2/pluscode_admin_v2.sqlite` 生成 APK assets。数据库不在代码仓库中；先从 Release 下载 `pluscode-admin-v2-sqlite.zip`，把其中的 SQLite 文件解压到上述路径。

使用 Android Studio 打开本目录，配置 JDK 17+、Android SDK Platform 35 和 Build Tools 35.0.0 后构建。命令行也可从上一层运行 `scripts/build.sh :app:assembleDebug`。

代码仓库不包含签名密钥。自行构建的 Debug APK 使用本机 Android 开发环境的调试签名；它与 Release 提供的 APK 签名可能不同，覆盖安装前请核对签名或先卸载旧版本。
