# Leaves 项目计划（.plan/）

> Leaves：极致性能与安全，为软件工程而生。
> 对标 Rust / Go，主打统一 Region 内存模型、结构化并发、低噪音 Trait 语法与目录即模块。

本目录是项目的**唯一真相来源（Single Source of Truth）**。
设计、决策、进度、风险都记录在这里，代码和规范文档与它冲突时，以这里为准，或先更新这里。

- 当前阶段：**Phase 0 → Phase 1（设计定稿，准备撰写规范）**
- 最后更新：`YYYY-MM-DD`
- 开发环境：Windows（命令示例使用 PowerShell）

---

## 1. 目录结构

```
.plan/
├── README.md                      # 本文件：索引、约定、使用方式
├── 00-vision.md                   # 愿景、定位、差异化、命名体系
├── 02-open-questions.md           # 开放问题与已知不一致（OQ-xxx）
├── 03-roadmap.md                  # 阶段路线图与任务清单（checkbox）
├── 04-risks.md                    # 风险登记册
├── 05-changelog.md                # 变更日志
├── glossary.md                    # 术语表（统一定义 region/sprout/scope 等核心词汇）
│
├── decisions/                     # 设计决策，每条独立成文件（ADR风格）
│   ├── 0000-template.md           # 决策文件模板
│   ├── DD-001-ownership-model.md
│   ├── DD-002-lifetime-inference.md
│   ├── DD-003-region-model.md
│   ├── DD-004-trait-generics-syntax.md
│   ├── DD-005-module-system.md
│   ├── DD-006-compile-architecture.md
│   └── DD-007-concurrency-model.md
│
├── rfcs/                          # 重大变更提案（修改已锁定决策时使用）
│   └── 0000-template.md
│
├── examples/                      # 真实 .lv 示例代码，供决策文档引用
│   ├── 01-hello-world.lv
│   ├── 02-ownership-move.lv
│   ├── 03-region-nested.lv
│   ├── 04-trait-inline-impl.lv
│   └── 05-structured-concurrency.lv
│
├── diagrams/                      # Mermaid 图表，纯文本、Git友好、GitHub可直接渲染
│   ├── compiler-pipeline.mmd
│   └── module-dependency-dag.mmd
│
└── scripts/                       # 辅助维护脚本
    └── plan_status.ps1            # PowerShell 版进度统计脚本
```

---

## 2. 快速索引

| 想了解……                   | 去看                                               |
| -------------------------- | -------------------------------------------------- |
| Leaves 是什么、为什么存在  | `00-vision.md`                                     |
| 某个特性为什么这样设计     | `decisions/DD-xxx-*.md`                            |
| 还有哪些没想清楚的问题     | `02-open-questions.md`                             |
| 现在做到哪了、下一步做什么 | `03-roadmap.md`                                    |
| 项目可能在哪里翻车         | `04-risks.md`                                      |
| 某个术语到底指什么         | `glossary.md`                                      |
| 想推翻或新增重大设计       | `rfcs/`                                            |
| 某个特性具体代码长什么样   | `examples/*.lv`                                    |
| 编译流程/依赖关系图        | `diagrams/*.mmd`（VS Code装Mermaid插件可直接预览） |

---

## 3. 项目速览

```yaml
name: Leaves
file_extension: .lv
config_file: Leaves.toml
toolchain:
  compiler: leafc
  package_manager: branch
  formatter: trim
  test_framework: bloom
  lsp: leaves-lsp
  runtime: grove
syntax: C 系花括号，函数关键字为 `function`
memory: 所有权系统 + 统一 Region 抽象（无 GC）
concurrency: 有栈协程(sprout) + 结构化并发(scope) + Grove 运行时
implementation_language: Rust（引导编译器）
backend: LLVM（inkwell）
incremental: Salsa 式查询架构
dev_os: Windows
```

### 已锁定决策一览

完整内容见 `decisions/` 目录下对应文件，此表仅做速查。

| ID     | 主题        | 一句话结论                                          | 状态                             |
| ------ | ----------- | --------------------------------------------------- | -------------------------------- |
| DD-001 | 内存管理    | 所有权系统，不使用 GC                               | ✅ Locked                        |
| DD-002 | 生命周期    | 自动推断为主，显式标注兜底；pub 函数签名必须自洽    | ✅ Locked                        |
| DD-003 | Region 模型 | 显式 `region`，支持嵌套、`<generational>`、`<sync>` | ✅ Locked                        |
| DD-004 | Trait/泛型  | 约束内联、取消 `where`，`given` 作逃生舱            | ✅ Locked                        |
| DD-005 | 模块系统    | 目录即模块；同目录可循环，跨目录强制 DAG            | ✅ Locked                        |
| DD-006 | 编译架构    | 函数级查询式增量编译，单态化全局去重                | ✅ Locked                        |
| DD-007 | 并发模型    | `sprout` + `grove::scope`，级联取消                 | 🧪 Provisional（见下方风险提示） |

> **注意**：DD-007 因有栈协程的栈内存管理（无GC场景）存在未验证的技术风险，状态已从 Locked 降级为 Provisional，详见 `04-risks.md` 与 `decisions/DD-007-concurrency-model.md`。

---

## 4. 文档约定

### 4.1 决策文件（decisions/DD-xxx）

- 每条决策独立成文件，使用 `decisions/0000-template.md` 模板
- 文件头使用 YAML frontmatter 记录元数据（id/status/date/related）
- **只增不改**内容本身。要推翻旧决策时：
  1. 新建一条新决策文件，`supersedes` 字段填旧 ID
  2. 旧文件状态改为 `Superseded`，`superseded_by` 字段填新 ID
  3. 不删除旧文件内容

状态取值：

| 状态           | 含义                     |
| -------------- | ------------------------ |
| ✅ Locked      | 已锁定，实现以此为准     |
| 🧪 Provisional | 暂定，需原型验证后再锁定 |
| ♻️ Superseded  | 已被新决策取代           |
| ❌ Rejected    | 讨论后否决               |

### 4.2 开放问题（02-open-questions.md）

- 编号：`OQ-001`……，字段：问题、优先级（高/中/低）、关联决策、预期解决阶段
- 解决后不删除，状态改为 `Resolved` 并链接对应 DD 文件

### 4.3 RFC（rfcs/）

- 满足任一条件必须走 RFC：修改已锁定的 DD、新增语言关键字、改变内存或并发语义
- 文件名：`rfcs/NNNN-short-title.md`，使用 `rfcs/0000-template.md` 模板
- RFC 通过后，在 `decisions/` 新增对应 DD 文件

### 4.4 示例代码（examples/）

- 所有 `.lv` 示例必须能被决策文档引用（而不是把代码直接写在文档里）
- 命名格式：`NN-short-description.lv`
- 后续 Phase 3 开发 parser 时，这些示例应当作为语法测试用例复用

### 4.5 图表（diagrams/）

- 统一使用 Mermaid 语法（`.mmd` 文件）
- GitHub 网页可直接渲染；本地推荐 VS Code + "Markdown Preview Mermaid Support" 插件

### 4.6 术语表（glossary.md）

- 所有新造词（region、sprout、scope、given 等）必须在此登记唯一定义
- 格式：`**术语** —— 定义（首次出现于 DD-xxx）`

### 4.7 其他约定

- 日期格式统一 `YYYY-MM-DD`
- 语言正式规范文档放在仓库根目录 `spec/`（尚未创建），**不放在 `.plan/` 里**
  - `.plan/` 回答"为什么、何时、决定了什么"
  - `spec/` 回答"具体是什么、语法规则细节"

---

## 5. 工作流程

```
每次开发会话开始：
  1. 运行 scripts\plan_status.ps1 查看整体进度
  2. 阅读 02-open-questions.md，确认有无阻塞问题
  3. 阅读 03-roadmap.md，选定本次任务

每次会话结束：
  4. 更新 roadmap 的 checkbox
  5. 新决策 → decisions/ 新增文件；新疑问 → 02-open-questions.md 追加
  6. 05-changelog.md 追加一行记录
```

### 查看进度（Windows / PowerShell）

```powershell
cd .plan
.\scripts\plan_status.ps1
```

> 若提示"无法加载脚本，因为在此系统上禁止运行脚本"，需先在 PowerShell（管理员）执行一次：
>
> ```powershell
> Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
> ```

---

## 6. 已知设计不一致与技术风险（需在 Phase 1 解决）

详细跟踪见 `02-open-questions.md`，此处列出核心摘要：

1. **生命周期显式标注语法未统一** —— Rust风格 `<'a>` 与 Region谓词 `region(a)==region(b)` 混用，需二选一并明确 `'static` 的对应概念
2. **有栈协程栈管理与无GC的根本冲突（高风险）** —— 见 `decisions/DD-007-concurrency-model.md`，需原型验证后才能转为 Locked
3. **`region<sync>` 与 Send/Sync 语义关系未定义**
4. **关联类型并入泛型参数后的推断歧义风险** —— 需验证 `Iterator` 等核心 trait 可表达性
5. **`<generational>` Region 引用与普通引用的类型区分方式未定**
6. **错误处理机制（Result + `?`）细节未定稿**，影响全部标准库 API 设计
7. **`dyn_generic` 实现成本与 trait 对象/增量编译的交互未明确**

---

## 7. 贡献与维护

- 当前为单人设计阶段，所有决策由项目发起人拍板
- 引入协作者后，RFC 流程转为评审制（至少一位非作者批准方可合并）
- 本 README 只维护索引与约定，具体内容更新在对应子文档，保持本文件相对稳定

```

```
