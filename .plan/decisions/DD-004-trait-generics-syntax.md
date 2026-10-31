---
id: DD-004
title: Trait / 泛型语法 —— 结构化约束内联
status: Locked
date: 2024-01-04
supersedes: null
superseded_by: null
related: [OQ-001, OQ-006, OQ-010, OQ-012]
---

## 决定

保留 Rust trait 系统的完整语义表达能力（关联类型、默认方法、trait 对象、运算符重载等），但通过语法重构消除语法噪音：取消 `where` 子句，约束内联到泛型参数声明处；允许 `impl` 内联写在 `struct` 定义内部；关联类型复用泛型参数语法。

## 背景与问题

Rust trait 系统语义强大，但语法分散在多处，带来三个具体痛点：

1. **到处散落的 where 子句**：复杂约束经常让人在函数签名和尾部 where 块之间来回跳读
2. **impl 块与 trait 定义视觉割裂**：结构体定义和它的 trait 实现经常相隔几十上百行
3. **泛型函数签名约束堆砌**：简单泛型函数也要写一堆尖括号和 trait bound

Leaves 选择"结构化约束"路线（而非仅语法糖整合，也非完全弱化为鸭子类型），在保留静态类型完整表达力的前提下重新设计语法摆放位置。

## 采纳方案

### Trait 定义：基本不变

```lv
trait Printable {
    function print(&self);
}
```

### Impl 内联到结构体定义处（新增能力，独立写法依然保留）

```lv
// 示例见 examples/04-trait-inline-impl.lv
struct Point {
    x: f64,
    y: f64,

    impl Printable {
        function print(&self) {
            println("({}, {})", self.x, self.y);
        }
    }
}

// 独立写法依然支持，可与内联写法在同一类型上混用
impl Comparable<Point> for Point { ... }
```

混用时编译器给出**非强制** lint 提示，建议团队风格统一，具体规则可在 `Leaves.toml` 配置。

### 泛型约束：永远内联，取消 where

```lv
function process<T: Clone + Debug, U: Into<T> + Send>(x: T, y: U) -> T {
    ...
}

// 约束较长时，使用结构化换行，而非退化到 where 子句
function process<
    T: Clone + Debug,
    U: Into<T> + Send + Sync,
>(x: T, y: U) -> T {
    ...
}
```

### 逃生舱：given 块

应对真正的复杂多向依赖约束（如 `T: Trait<U>, U: Trait<T>` 循环依赖）：

```lv
function complex<T, U>(x: T, y: U) -> T
given {
    T: Trait<U>,
    U: Trait<T>,
    T::Output == U::Output,
}
{
    ...
}
```

具体语法细节（分隔符、嵌套作用域规则）见 **OQ-001（未决）**。

### 关联类型复用泛型参数语法

```lv
trait Container<Item> {
    function get(&self, i: usize) -> Item;
}

struct MyVec {
    data: Vec<i32>,

    impl Container<i32> {
        function get(&self, i: usize) -> i32 {
            return self.data[i];
        }
    }
}
```

**默认行为**：每个类型只能 `impl Container<X>` 一次（关联类型唯一性语义）。

**解除限制**：`trait Container<Item: multi>` 标注后，允许同一类型多次实现不同参数（对应 Rust 中泛型 trait 的多态能力，如 `From<T>` 场景）。

```lv
trait From<Source: multi> {
    function from(source: Source) -> Self;
}

impl From<f64> for Celsius { ... }
impl From<i32> for Celsius { ... }   // multi 标注后允许
```

### Trait 对象：保持不变

```lv
function print_it<T: Printable>(item: T) { item.print(); }       // 静态分发
function print_it(item: dyn Printable) { item.print(); }         // 动态分发，显式标注
```

## 被否决的备选方案

| 方案                                                | 为什么被否决                                                                                         |
| --------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| 方案A：语义完全不变，只做语法糖整合                 | 不足以解决 where 子句分散的根本问题，差异化程度不够                                                  |
| 方案C：弱化显式impl，TypeScript式结构化类型自动满足 | 牺牲了静态类型系统的精确性和可预测性，与"安全"核心定位冲突                                           |
| 关联类型默认允许多实现，唯一性需显式标注            | 与 Region 系统"安全默认，显式逃生舱"的整体设计哲学不一致；也更容易在复杂泛型代码中产生难以察觉的歧义 |
| 完全取消 impl 独立写法，强制内联                    | 大型项目/多文件组织场景下缺乏灵活性，不适合"软件工程"定位                                            |

## 影响范围

- 类型检查器中的 trait 求解算法需要适配新的约束收集方式（不再有统一的 where 子句扫描点）
- 标准库 trait 设计（尤其 `From`/`Into`/`Iterator`）需要验证新语法的表达力
- 与 DD-006（增量编译）存在交互：`dyn_generic` 逃生舱的实现需要明确与 `dyn Trait` 的关系

## 验证方式

- 使用真实标准库场景（`Iterator`、`From`/`Into`）做纸面验证，确认语法改动后依然可以无歧义表达
- Phase 3 MVP 中优先实现内联约束和独立 impl，内联 impl 和 given 块可延后到后续迭代

## 未决问题

- **OQ-001**：given 块具体语法细节
- **OQ-006**：From/Into 等核心 trait 的 multi 标注落地验证
- **OQ-010**：关联类型并入泛型参数的推断歧义风险（需 Iterator 场景验证）
- **OQ-012**：dyn_generic 与 dyn Trait 的实现关系
