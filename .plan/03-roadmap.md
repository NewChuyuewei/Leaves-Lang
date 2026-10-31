# 03. 路线图

```
[ Phase 0 ] 设计定稿        ← 当前阶段
     ↓
[ Phase 1 ] 语言规范文档
     ↓
[ Phase 2 ] 编译器架构设计 & 技术选型
     ↓
[ Phase 3 ] MVP 编译器（无Region/无并发）
     ↓
[ Phase 4 ] 核心特性扩展（Region/Grove运行时）
     ↓
[ Phase 5 ] 工具链（LSP/包管理器/格式化）  ← 与Phase3/4并行
     ↓
[ Phase 6 ] 标准库与生态
```

---

## Phase 0：设计定稿 ✅ 基本完成

- [x] 确定核心定位（软件工程优先，对标 Rust/Go）
- [x] 确定内存模型方向（所有权 + Region）
- [x] 确定并发模型方向（有栈协程 + 结构化并发）
- [x] 确定 Trait/泛型语法方向（内联约束）
- [x] 确定模块系统方向（目录即模块）
- [x] 建立 `.plan` 目录与文档体系
- [ ] 解决 OQ-007（生命周期标注语法统一）—— 阻塞 Phase 1 启动

---

## Phase 1：语言规范文档

**目标**：产出完整、无歧义的语言规范，作为后续所有工作的基石。

### 文档交付

- [ ] `spec/grammar.ebnf` —— 完整语法规范
- [ ] `spec/type-system.md` —— 类型系统 + 生命周期推断算法（需先解决 OQ-007, OQ-010）
- [ ] `spec/ownership.md` —— 所有权与借用检查规则
- [ ] `spec/region-model.md` —— Region 系统详细语义（需先解决 OQ-011）
- [ ] `spec/concurrency.md` —— Sprout/Scope/Grove 运行时语义（需先解决 OQ-008, OQ-009）
- [ ] `spec/error-handling.md` —— Result/panic/错误传播机制（解决 OQ-005）
- [ ] `spec/stdlib-core.md` —— 核心标准库 API 契约（验证 OQ-006）

### 验证环节

- [ ] 在 `.plan/examples/` 补充至少 15 个覆盖全部已设计特性的示例程序
- [ ] 纸面验证：手动推导示例程序的类型检查/借用检查过程，记录发现的规范矛盾到 `02-open-questions.md`
- [ ] 用真实 `Iterator` trait 场景验证关联类型语法（OQ-010）

### 阻塞前置

- [ ] OQ-007（生命周期标注语法）必须先解决
- [ ] OQ-005（错误处理机制）必须先解决

---

## Phase 2：编译器架构设计

- [ ] 技术预研：有栈协程栈管理方案原型验证（解决 OQ-003, OQ-008）
- [ ] 确定实现语言：Rust（已倾向，待 Phase 2 正式确认）
- [ ] 确定解析策略：手写递归下降 + Pratt 解析
- [ ] 确定增量计算框架：评估 `salsa` crate 适配性
- [ ] 确定后端：LLVM（`inkwell` 绑定）
- [ ] 绘制完整编译流水线架构图（更新 `diagrams/compiler-pipeline.mmd`）
- [ ] 搭建 Rust workspace 项目骨架

---

## Phase 3：MVP 编译器

**里程碑目标**：编译运行一个 CLI 版"待办事项管理器"

### 第一批：基础可用

- [ ] 基本类型：i32 / f64 / bool / String
- [ ] 函数定义、调用
- [ ] struct + 基础方法（无 trait）
- [ ] if/else, loop/while, 基础模式匹配
- [ ] 简单所有权检查（move 语义，无借用）

### 第二批：所有权完整

- [ ] 引用 & 借用检查（第一级生命周期推断规则）
- [ ] Vec / Option / Result 标准库最小实现

### 第三批：类型系统完整

- [ ] Trait 定义与实现（内联 + 独立两种写法）
- [ ] 泛型函数与约束求解

### 里程碑验收

- [ ] 集成测试套件跑通 MVP 示例程序（待办事项管理器）
- [ ] 基础错误信息可读性达到可用标准

---

## Phase 4：核心特性扩展

- [ ] Region 系统完整实现（含嵌套生命周期验证算法）
- [ ] Generational Region 运行时支持
- [ ] 第二级生命周期推断（私有函数体数据流分析 / borrow provenance tracking）
- [ ] Grove 运行时：有栈协程栈切换（优先 x86_64 Linux + Windows）
- [ ] Scope = Region 统一借用检查扩展
- [ ] 增量编译完整落地与性能调优
- [ ] region\<sync> 自动同步策略原型（解决 OQ-002）

---

## Phase 5：工具链（与 Phase 3/4 并行）

- [ ] `leafc` 编译器打包发布流程
- [ ] `branch` 包管理器（参考 cargo 架构）
- [ ] `trim` 格式化工具
- [ ] `bloom` 测试框架
- [ ] `leaves-lsp`（**建议 Phase 3 MVP 跑通后立刻启动**，优先支持语法高亮 + 简单跳转）

---

## Phase 6：标准库与生态

- [ ] 核心集合：Vec / HashMap / String / Option / Result
- [ ] IO 与文件系统
- [ ] 网络库
- [ ] 异步生态：HTTP client/server 框架
- [ ] 序列化（JSON 等）
- [ ] C FFI 互操作层（降低生态冷启动门槛）

---

## 本周行动项（持续更新此区块）

```
[ ] 1. 解决 OQ-007：确定生命周期显式标注的最终语法
[ ] 2. 解决 OQ-005：敲定错误处理机制基本形态
[ ] 3. 开始撰写 spec/grammar.ebnf 草稿
```
