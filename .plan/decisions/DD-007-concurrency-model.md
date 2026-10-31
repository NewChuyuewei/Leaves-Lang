---
id: DD-007
title: 并发模型 —— 有栈协程 + 结构化并发
status: Provisional
date: 2024-01-07
supersedes: null
superseded_by: null
related: [DD-001, DD-003, OQ-003, OQ-008, OQ-009]
---

## ⚠️ 状态说明

本决策状态为 **Provisional（暂定）而非 Locked**。核心风险在于：有栈协程的栈内存管理与"无 GC"的内存模型之间存在尚未验证的根本性技术张力（详见下方"未决问题"与 `04-risks.md` 中的 R-002）。**本决策需要在 Phase 2 之前完成独立原型验证，才能转为 Locked。**

## 决定

Leaves 放弃 Rust 式的 Future/Poll 状态机异步模型，采用有栈协程（Stackful Coroutine，命名为 `sprout`）作为并发基本单元，由内置的 `grove` 运行时进行 M:N 调度。并发边界通过 `grove::scope` 实现结构化并发，且 scope 同时也是一个 Region 边界，复用借用检查器的既有逻辑。标准运行时接口支持替换（双轨制：默认内置 + 可选自定义）。

## 背景与问题

Rust 异步模型（`async fn` 编译为状态机）的核心痛点均源于其无栈协程的本质：

1. **Pin/Unpin 心智负担**：跨越 `await` 点的局部变量借用会产生自引用结构，必须用 `Pin` 固定内存地址
2. **函数彩色问题**：`async fn` 和普通 `fn` 是两个不兼容的类型系统，不能自由互相调用
3. **`'static` 约束痛点**：`tokio::spawn` 要求捕获数据必须 `'static`，因为运行时无法证明 spawn 出去的 Future 不会比借用数据活得更久
4. **运行时生态割裂**：tokio / async-std / smol 三足鼎立，库作者被迫做运行时无关抽象或强绑定某个运行时

Go 的 goroutine（有栈协程）天然不存在上述 1-3 的问题，但 Go 依赖 GC 实现**可增长栈的安全搬移**，这是 Leaves（无GC）无法直接照搬的关键难点。

## 采纳方案

### 有栈协程替代状态机

```lv
// 示例见 examples/05-structured-concurrency.lv
// 没有 async/await 关键字，写法等同普通同步代码
function fetch_user(id: u64) -> User {
    let data = http::get(format!("/users/{}", id));  // 协作式让出，不阻塞线程
    return User::parse(data);
}

function main() {
    let handle = sprout { fetch_user(42) };
    let user = handle.join();
}
```

这一设计直接消除：

- Pin/Unpin（没有状态机生成，不存在自引用问题）
- 函数彩色问题（没有独立 Future 类型，同步异步可自由互调）

### 结构化并发：Scope = Region 边界

```lv
let data = vec![1, 2, 3];

grove::scope(|s| {
    s.sprout(|| process(&data));  // 合法：scope 被借用检查器视为一个 region
    s.sprout(|| process(&data));
});  // scope 退出时自动等待所有子 sprout 完成
```

原理：`grove::scope` 创建的边界同时是并发边界和 Region 边界，子 sprout 对外层数据的借用天然满足 DD-003 中"内层不长于外层"的嵌套规则，无需为并发场景设计额外的借用检查规则。

### Channel：所有权转移语义

```lv
let (tx, rx) = channel::<Vec<i32>>();
let data = vec![1, 2, 3];
tx.send(data);        // 所有权转移
// data.push(4);       // 编译错误：data 已被移动
```

直接在编译期杜绝 Go channel 设计中"发送后继续修改原数据"导致的隐藏数据竞争。

### 规则细节

- **裸 spawn 需显式标注**：默认推荐 `grove::scope` 结构化写法；真正需要脱离作用域的后台任务，必须显式调用 `grove::spawn_detached`（命名带有警示性）
- **级联取消**：scope 内任一子 sprout panic，自动取消该 scope 内其余未完成的兄弟任务，并向上传播错误
- **Region 并发标注**：`region X<sync>` 显式开启跨 sprout 共享，默认 Region 仅限创建它的线程/sprout 内访问
- **标准化 Runtime 接口**（落实"方案C"双轨制）：

```lv
trait Runtime {
    function spawn<F: Send>(task: F) -> Handle<F::Output>;
    function block_on<F>(task: F) -> F::Output;
}
```

默认内置 `grove` 实现，`Leaves.toml` 中可替换为自定义实现，服务于嵌入式/WASM/特殊调度场景

- **底层逃生舱**：保留 `unsafe_async` 供运行时/库作者实现零成本抽象场景使用，日常业务代码不会接触

## 被否决的备选方案

| 方案                                                     | 为什么被否决                                                                                  |
| -------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| 沿用 Rust Future/Poll 状态机模型                         | 无法解决 Pin/Unpin 和函数彩色问题，这是 Leaves 想要解决的核心 Rust 痛点，照搬则失去差异化价值 |
| Actor 模型作为核心并发原语                               | 学习曲线陡峭，且与既有的所有权/Region 模型整合复杂度高，不如 sprout+scope 方案自然            |
| 并发运行时完全独立，不设计标准接口（纯 Go 式唯一运行时） | 丧失嵌入式/WASM等特殊场景的灵活性，之前已确定选择"方案C"双轨制                                |
| 完全开放式接口，不提供默认运行时（纯 Rust 现状）         | 重蹈生态割裂覆辙，违背"终结选择困难"的设计目标                                                |
| 裸 spawn 完全禁止（强制100%结构化）                      | 过于severe，无法表达真正合理的"fire and forget"后台任务场景（日志刷盘、心跳检测等）           |

## 影响范围

- 与 DD-003（Region模型）深度耦合：scope 直接复用 Region 的借用检查逻辑
- 需要平台相关的底层实现（栈切换机制），是编译器+运行时中技术难度最高的部分
- 标准库网络/IO相关 API 设计必须基于 sprout 模型（不能是 Future-based）

## 验证方式

**本决策转为 Locked 的前提条件**：

1. 完成 OQ-008 的独立原型验证——证明存在可行的无GC有栈协程栈管理方案
2. 在至少 x86_64 Linux + Windows 平台验证栈切换机制的正确性与性能
3. 用真实的"数千并发任务"场景做压力测试，验证内存占用处于可接受范围

若原型验证证明技术方案不可行或代价过高，需启动 RFC 重新评估，可能的 Plan B 方向包括：有限形式的无栈协程（但尝试结合 Region 模型简化部分 Rust 痛点）、或混合模型（关键路径有栈协程+IO密集路径有限状态机）。

## 未决问题

- **OQ-003**（高优先级，阻塞）：跨平台有栈协程栈切换具体实现方案
- **OQ-008**（高优先级，进行中）：有栈协程栈管理与无GC的根本冲突，候选方案包括 mmap懒提交、分段栈、固定栈+溢出检测
- **OQ-009**：`region<sync>` 与 Send/Sync 语义关系
- 关联风险：**R-002**（🔴 高，当前项目最大技术不确定性）
