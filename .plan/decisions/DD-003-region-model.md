---
id: DD-003
title: 统一 Region 内存模型
status: Locked
date: 2024-01-03
supersedes: null
superseded_by: null
related: [DD-001, DD-002, DD-007, OQ-002, OQ-009, OQ-011]
---

## 决定

引入 `region` 作为语言核心概念，统一表达"内存生命周期边界"。Region 分为隐式（编译器自动管理，对应传统 lifetime）和显式（开发者手写 `region { }` 块）两种形式，支持嵌套、世代引用（generational）、跨线程共享（sync）三个维度的扩展标注。

## 背景与问题

Rust 的 lifetime 系统在编译器内部本质上就是用"区域"（Region）实现的，但这个概念从未对用户暴露——用户看到的只是抽象的 `'a` 符号，以及完全独立的、需要额外引入第三方库（如 `typed-arena`、`bumpalo`）才能使用的 Arena 分配模式。

这造成两个割裂的系统：

1. 隐式的、编译器内部的 lifetime/region
2. 显式的、需要手动管理的 Arena 库

Leaves 希望统一这两者，让"区域"成为开发者可以直接感知和操作的一等公民概念，同时解决 Rust 公认的痛点场景：**自引用结构体**（如链表、图），这类结构在 Rust 中极其痛苦，通常需要 `Rc<RefCell<>>` 或 `unsafe` + `Pin`。

## 采纳方案

### 隐式 Region（自动管理）

对应 DD-002 中描述的生命周期推断机制，编译器自动管理，开发者无需感知。

### 显式 Region（手动声明）

```lv
// 示例见 examples/03-region-nested.lv
region Graph {
    struct Node {
        value: i32,
        edges: Vec<&Node>,   // 同region内引用，无需生命周期标注
    }
}

function build_cyclic_graph() -> Owned<Graph> {
    let g = Graph::new();
    let a = g.alloc(Node { value: 1, edges: vec![] });
    let b = g.alloc(Node { value: 2, edges: vec![] });
    a.edges.push(b);
    b.edges.push(a);   // 安全的环状引用，Rust中需要 Rc<RefCell<>> 或 unsafe
    return g;
}
```

### 规则细节

**基本语义**：

- 同一 `region` 块内的所有分配共享同一个生命周期边界
- Region 内部引用无需单独标注生命周期——保证同生共死
- Region 作为单一所有权单元对外暴露，外部只能通过"根引用"访问
- Region 整体销毁时，内部所有对象批量释放（零成本，适合游戏关卡/Web请求等场景）

**嵌套规则**：

- 内层 Region 的生命周期必须被外层 Region 完全包含（内层 ⊆ 外层）
- 允许内层持有指向外层 Region 对象的引用，反之不允许
- 编译器需验证：内层引用不会逃逸到外层 Region 释放之后

```lv
region World {
    struct GameWorld { ... }

    region Level {   // 嵌套：Level 不能比 World 活得久
        struct LevelData {
            parent_ref: &World::GameWorld,  // 允许引用外层
        }
    }
}
```

**单对象删除：Generational Region**（修饰符 `<generational>`）：

```lv
region Pool<generational> {
    struct Entity { hp: i32 }
}

function tick(pool: &Pool<Entity>, id: GenRef<Entity>) {
    match pool.get(id) {
        Some(e) => e.hp -= 1,
        None => print("entity already removed"),  // 运行时安全检查，非UB
    }
}
```

原理：每个引用携带世代计数器，对象被移除后世代号失效，访问返回 `Option` 而非悬空指针。代价是极小的运行时开销（一次整数比较）。

**并发标注**（修饰符 `<sync>`）：

```lv
region Cache { ... }          // 默认：仅限创建它的线程/sprout访问
region Cache<sync> { ... }    // 显式标注：允许跨线程共享，编译器自动插入同步机制
```

具体同步策略选择算法见 OQ-002（未决）。

## 被否决的备选方案

| 方案                                                               | 为什么被否决                                                                                        |
| ------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------- |
| 维持 Rust 现状（lifetime标注 + 第三方Arena库）                     | 两套割裂系统，且自引用结构问题依然无解，没有体现差异化价值                                          |
| 完全自动化，不暴露显式 region 语法                                 | 丧失了解决自引用结构体问题的能力，这是对 Rust 最有力的差异化卖点                                    |
| 世代引用做成独立标准库类型（如 GenPool\<T>），不与 region 语法绑定 | 这样和 Rust 生态中已有的 `slotmap` 库没有本质区别，无法体现"语言级地基"的统一性价值                 |
| 完全禁止跨 Region 引用                                             | 会严重损害可组合性，不适合"软件工程"场景下自然需要的层级化数据结构（如游戏引擎 World→Level→Entity） |

## 影响范围

- 借用检查器需要扩展以支持 Region 嵌套验证（这是 R-001 风险的主要来源）
- 与并发模型（DD-007）深度耦合：`scope` 本质上也是一种 Region 边界
- 标准库的核心数据结构设计需要考虑 Region 友好的 API（如 `Graph::alloc`）

## 验证方式

- Phase 4 阶段实现完整 Region 系统前，需要先完成纸面的嵌套规则形式化验证
- 使用真实的图/链表场景（`examples/03-region-nested.lv`）作为验收标准
- 参考学术界已有的 Region 类型系统论文（如 Cyclone 语言的 Region 系统）降低自研风险

## 未决问题

- **OQ-002**：`region<sync>` 的自动同步策略选择算法
- **OQ-009**：`region<sync>` 与 Send/Sync 语义关系
- **OQ-011**：Generational Region 引用（GenRef）与普通引用的类型系统区分方式
- 关联风险：**R-001**（Region 嵌套借用检查算法复杂度）
