<div align="center">

<img src="assets/icon.svg" alt="S3 Desktop" width="96" height="96">

# S3 Desktop

**一款快速的原生 S3 兼容对象存储桌面客户端。**

UCloud US3 · AWS S3 · MinIO · 以及任何实现了 S3 API 的服务

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform: Windows 11](https://img.shields.io/badge/platform-Windows%2011-0078D4.svg)](#构建)
[![Qt 6](https://img.shields.io/badge/Qt-6.9-41CD52.svg)](https://www.qt.io/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](#构建)

[English](README.md) | **中文**

</div>

---

本项目是对一个 MIT 许可的早期 Go/Fyne 版 S3 兼容存储客户端的 **Qt 重写版**。
它不是移植：原版 Go/Fyne 代码没有一行被翻译过来。全部功能基于 Qt 6 与自行
实现的 SigV4 签名器从零重写，因为原版的结构——一个 1150 行、从后台 goroutine
调用厂商 SDK 的单一窗口——正是它的缺陷难以修复的根源。

## 目录

- [功能](#功能)
- [与原版的差异及原因](#与原版的差异及原因)
- [构建](#构建)
- [静态编译](#静态编译)
- [测试](#测试)
- [日志](#日志)
- [目录结构](#目录结构)
- [版本](#版本)
- [许可证](#许可证)

## 功能

| | |
|---|---|
| **连接管理** | 本地保存的命名配置：endpoint、Access Key、Secret Key、桶、区域、前缀、TLS、寻址方式。对话框内置「测试连接」。 |
| **浏览** | 先列桶，再显示按文件夹归组的对象表；面包屑导航，支持后退/前进/上级（`Alt+←` / `Alt+→` / `Alt+↑`）。 |
| **桶根目录** | 连接未填写桶时，根目录直接展示该账号的桶列表——它是独立的一层，有自己的面包屑，而不是某个 Key 前缀的替身。打开某个桶后显示其对象，路径变为 `Buckets › 桶 › 目录 › …`；点击面包屑或按 `Alt+↑` 可退回桶列表。 |
| **搜索与排序** | 对已加载对象做客户端过滤；按名称、大小或修改时间排序，遵循区域设置并识别数字。文件夹始终排在文件之前。 |
| **分页** | 每次加载 500 个对象，从上一页结束处继续；「加载更多」为追加而非替换，因此 5 万对象的桶依然流畅。 |
| **详情** | 显示选中对象的 Key、大小、修改时间、ETag 与存储类型。 |
| **传输** | 上传/下载队列，逐项显示进度并可取消；走 `QNetworkAccessManager`，UI 线程永不阻塞。 |
| **分享** | 本地生成一小时有效的预签名 GET 链接，无需往返请求。 |
| **桶管理** | 在独立窗口创建与删除桶，附带客户端名称校验。 |
| **命令行** | `--endpoint`、`--access-key`、`--secret-key`、`--bucket`、`--prefix`、`--region`、`--target`、`--ssl` / `--no-ssl`。均回落到原版使用的 `ENDPOINT`、`ACCESS_KEY`、`SECRET_KEY` 等环境变量。 |

## 与原版的差异及原因

**不依赖厂商 SDK。** 签名使用 `QCryptographicHash` 与
`QMessageAuthenticationCode`，严格按公开的 SigV4 规范计算，并在测试套件中
以 AWS 官方测试向量验证。因此当服务端拒绝请求时，能区分是服务端的问题还是
签名器算错了。

**无线程。** 所有请求都走事件循环，响应天然回到 GUI 线程。原版在 goroutine
中调用 UI 工具包，这正是它大部分稳定性问题的来源。

**凭据存入 DPAPI。** Secret Key 使用 `CryptProtectData`（Windows 数据保护
API）加密，只有保存它的用户账户能读取。原版是明文 JSON。`CredentialStore`
是一个单一接口，接入其它平台的密钥环无需改动调用方。

**厂商差异是数据，不是分支。** `TargetProfile` 描述各厂商如何组织主机名与
推导区域——US3 的 `s3-<region>.ufileos.com`、AWS、MinIO、通用 path-style——
而不是在传输层到处散布 `if provider ==`。

**虚拟化的表格。** `ObjectModel` 对已加载对象只维护一份索引列表，从不复制
对象；`QTableView` 仅渲染可见行。固定行高让 Qt 无需逐行询问模型即可算出可见
范围，这是超长列表仍可用的关键。

## 构建

**依赖：** Qt 6（Core、Gui、Widgets、Network，测试套件另需 Test）、
CMake 3.21+ 以及支持 C++17 的编译器。

已在 Windows 11 上以 Qt 6.9.3（mingw_64）、GCC 13.1.0、CMake 4.0.1、
Ninja 验证通过。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw_64
cmake --build build
```

> [!NOTE]
> 产物为 `build/s3desktop.exe`。Windows 下按 GUI 子系统编译，不会附带控制台窗口。

## 静态编译

单个自包含的 `.exe`，旁边不需要任何 Qt DLL——无需安装、无需配 `PATH`、
也不会因为缺少运行库而启动失败。

Qt 官方 MinGW 包提供的是**导入库**而非静态库：其 `libQt6Core.a` 中每个成员都叫
`Qt6Core_dll_*.o`，因此 `-static` 只能解析引用，无法消除对 DLL 的依赖。真正
静态的 Qt 必须从源码构建：

```sh
# 在 qtbase 源码树中执行 configure.bat
configure.bat -static -static-runtime -release -opensource -confirm-license \
              -nomake examples -nomake tests -no-dbus -no-icu -platform win32-g++
# 然后用 Ninja
cmake --build . --parallel
cmake --install . --prefix C:/Qt/6.9.3/mingw1310_64_static
```

随后让工程指向该前缀并打开开关：

```sh
cmake -S . -B build-static -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/mingw1310_64_static \
      -DS3DESKTOP_STATIC=ON
cmake --build build-static
```

`S3DESKTOP_STATIC` 默认为关，且刻意不从 Qt 安装推断：静态 Qt 是另一套前缀，
若从 `Qt6Core_LIBRARIES` 猜测，`CMAKE_PREFIX_PATH` 一旦改指别处，构建方式就会
在无人察觉的情况下改变。

> [!IMPORTANT]
> 静态 Qt **默认不链接任何插件**。若不导入 Windows 平台插件，程序能正常编译
> 链接，却在启动时报 *「no Qt platform plugin could be initialized」* 而退出。
> 构建中按类型显式导入了 `QWindowsIntegrationPlugin`、`QModernWindowsStylePlugin`、
> `QSchannelBackendPlugin`（TLS）以及 ICO/JPEG/GIF 图像格式；MinGW 下同时传入
> `-static`，使 libgcc、libstdc++ 与 libwinpthread 一并静态绑定，而不是留作 DLL。

结果：`s3desktop.exe` 约 51 MB，仅导入 Windows 系统 DLL——没有
`Qt6*.dll`、没有 `libgcc_s_seh-1.dll`、没有 `libstdc++-6.dll`、
没有 `libwinpthread-1.dll`。

## 测试

```sh
ctest --test-dir build --output-on-failure
```

> [!IMPORTANT]
> 测试程序动态链接 Qt，因此需要把 Qt 的 `bin` 目录加入 `PATH`
> （`C:/Qt/6.9.3/mingw_64/bin`）；否则会在运行任何用例之前以
> `0xc0000135` 退出。以 `-DS3DESKTOP_STATIC=ON` 构建时无此要求——
> `PATH` 中只有 `C:\Windows\system32` 也能跑完整套件。

六个测试套件，均不需要显示器或网络：

| 套件 | 覆盖内容 |
|---|---|
| `test_sigv4` | 签名器，对照 AWS 官方示例与测试套件向量 |
| `test_list_parser` | XML 列表响应解析，含厂商差异与错误码映射 |
| `test_target_profile` | 各厂商的主机名组织与区域推导 |
| `test_transfer_queue` | 传输状态机 |
| `test_object_model` | 过滤、文件夹归组、排序，以及桶列表模式 |
| `test_object_browser` | 后退/前进/上级、各层级的面包屑，以及对象命令允许看到哪些行 |

## 日志

每次运行都会在 `settings.json` 同目录写入 `s3desktop.log`——Windows 下为
`%LOCALAPPDATA%\s3desktop\s3desktop\`。上一次的日志保留为
`s3desktop.log.1`。

日志记录每次请求真实拼出的 URL、真实参与签名的主机、使用的端口，以及凭据
存储对密钥的判断结果。失败时追加 HTTP 状态码、服务端 `<Code>` 与请求 ID，
以及响应体——签名类问题只有响应体会写明真正的原因。密钥会脱敏为前四后二。

`S3DESKTOP_LOG` 可指定路径，设为 `off` 则关闭日志。

## 目录结构

```
src/core/     传输、签名、配置、凭据、传输队列  （不含 GUI）
src/compat/   厂商配置
src/ui/       控件、模型、主题
tests/        Qt Test 测试套件
```

`core` 与 `compat` 只链接 Qt Core/Network，因此所有「出错也不易被用户察觉」
的逻辑都能独立测试。`ui` 单独编译为静态库，使模型与主题可以在
`QApplication` 下被验证，而无需打开主窗口。

## 版本

`VERSION` 保存当前版本号，每行一个裸版本号。**检查更新**动作会从本仓库默认
分支抓取该文件，与编译进程序的版本比对；若当前构建更旧，则询问是否打开项目
页。发布新版本时需同时修改 `VERSION` 与 `src/ui/MainWindow.cpp` 中的
`kVersion`。

## 许可证

MIT，与原版一致。详见 [LICENSE](LICENSE)。
