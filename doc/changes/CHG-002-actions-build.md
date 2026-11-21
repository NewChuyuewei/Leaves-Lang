# CHG-002：GitHub Actions 自动构建与产物归档

状态：工程质量变更，不指定 DAL，不声明认证。

## 需求与验收（实现前确定）

| 标识 | 需求 | 验证方式 |
| --- | --- | --- |
| ACT-001 | push、pull_request 和手动触发构建 | 审查工作流触发配置；远端执行待验证 |
| ACT-002 | 保留 Linux、严格 C89/C99 和内部断言检查，增加 macOS POSIX 构建 | 矩阵配置；每项执行完整仓库回归与示例 |
| ACT-003 | 只有构建、测试、安装后运行及归档重解压检查通过才上传可下载产物 | 顺序执行且失败中止；本地 Linux 打包链验证 |
| ACT-004 | 包含 leav、leavc、静态库、头文件、文档、示例和构建版本信息，按配置/架构命名 | 检查包清单和构建元数据；验证解压后的执行文件与字节码编译 |
| ACT-005 | 工作流不获得发布写权限，不把检查包当正式发行版 | contents: read；只上传 Actions artifact；文档说明依赖与平台限制 |

## 设计与影响

- 修改现有 CI，不创建重复的 push/PR 工作流；加入 workflow_dispatch。
- 每个矩阵任务使用独立 runner，保留 fail-fast: false，单任务设置超时。
- 用显式 Python 版本运行回归测试；打包复用既有 make install 和新的脚本。
- 新脚本只负责打包与冒烟验证，不修改 C 源码、语言语义或编译选项。
- 归档名含配置和运行器架构；包内记录实际 checkout 提交、系统、编译器、平台和额外选项。
- macOS 使用 posix 目标，不声称覆盖带 readline 的 macosx 配置。
- 不包括 Windows 适配、交叉编译、签名、Release 自动发布、静态链接独立二进制或认证证据库。

## 验证记录

- 基线：`b4dfb5c18b6a9eddeffb6df6f03df45b53a2660c` 加本次工作区变更。
- ACT-001/002：配置审查包含三种触发方式、五个矩阵任务；PyYAML 解析及配置断言通过。
- ACT-003/004：本地 Linux 默认、严格 C89、严格 C99、内部断言配置各通过
  49 项回归，随后安装打包、重解压、版本/示例及字节码编译执行检查通过。
- ACT-005：配置仅授予 contents: read，使用 upload-artifact@v4 保留 14 天，
  没有自动 Release/写权限步骤；README 记录下载方法及平台依赖限制。
- 本地环境：Linux x86_64、GCC 12.2.0、Python 3.11.2；托管环境显式选用 Python 3.12。
- 本地日志/包在忽略的 `.work/package-*.log` 和 `.work/artifacts/`，不提交二进制。
- `git diff --check` 通过。包校验文件与实际 SHA-256 一致；非法配置名被拒绝。
- 尝试获取 actionlint 时下载受网络限制，未运行 actionlint，不声称完成 Actions 专用 lint。

复现每个 Linux 配置：先 make clean，选择 make linux / iso-c89 / iso-c99；
断言配置使用 make linux MYCFLAGS='-DLUAI_ASSERT -DLUA_USE_APICHECK'。
之后运行 `python3 test/leaves/run.py`，再调用 test/package.py 传入对应
configuration、platform、flags。打包脚本用 subprocess 参数列表调用命令，
不会把选项拼接成 shell 脚本；但编译器/选项元数据是调用者声明，不是对象文件审计。

**待验证：**GitHub 托管 runner 的实际执行、artifact 上传/下载和 macOS 构建。
本地 Linux 验证不能替代这些验证；PyYAML 也不验证完整 GitHub Actions 语义。
独立人工评审尚未完成。

## 扩展需求：Windows x64 测试产物（实现前补充）

- WIN-001：在 windows-2022 + MSYS2 UCRT64 上构建原生 leav.exe/leavc.exe，执行完整回归。
- WIN-002：以 ZIP 提供 exe、静态库、头文件、示例、文档与构建元数据，并附 SHA-256。
- WIN-003：在独立 PowerShell 步骤清空工具链 PATH，仅保留 Windows 系统目录，
  解压后运行版本、示例及字节码编译执行；任何非零退出禁止上传。
- WIN-004：Windows 异常退出码不能被测试运行器当作预期语法错误通过。

采用单独 mingw-static 目标，把 Lua 和 GCC 支持库链接进 exe，不分发自有 Lua DLL。
此测试包面向 Windows 10/11 x64（依赖系统 UCRT），不保证 C 扩展 DLL 互操作、
32 位 Windows、Windows ARM64 原生运行或旧版 Windows。原有 mingw DLL 目标不属于本次适配。

Windows 适配还修正了对象字符串测试对 Unix `%p` 的 0x 前缀假设；
运行时仍采用 ISO C 平台指针格式。首次远端执行报告 macos-14 配置不受支持，
因此 macOS runner 调整为 macos-15，结果以对应提交的任务状态为准。
