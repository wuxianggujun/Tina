# 结构化序列化与多态

`include/tina/serialization/` 是 Core-only 的游戏数据层，内部 OBJECT 分组 `Tina::Serialization`
随唯一 `Tina::GameSDK` 发布。它不依赖 Save、Scene、UI，不要求游戏类型继承 Bundlable，
也不建立全局 registry。决策见 [ADR 0066](adr/0066-sprite-color-bitmap-text-and-serialization.md)。

## 模块与数据流

```text
游戏 owner 捕获一致 DTO / ObjectTable
  -> Codec<T> / 实例级 TypeRegistry<Base>
  -> encodeJson / encodeJsonWith -> owning bytes
  -> SaveStore::saveSlot / beginSave

SaveStore::loadSlot -> metadata + bytes
  -> 游戏检查 dataVersion（需要时显式调用产品 migration）
  -> decodeJson -> detached DTO / ObjectTable
  -> resolve ObjectId -> 游戏不变量校验
  -> 单次替换 live state
```

存储 envelope、archive schema 和游戏 `dataVersion` 是三个不同职责。SaveStore 继续只保存字节；
JSON 当前唯一格式是 `{"schema":1,"data":...}`，其它 schema、缺少 envelope 或重复 JSON key 直接失败，
不嗅探旧格式、不自动回落，也不把 decode 失败解释成“新游戏”。无需数据库。

## Codec 接口

`Codec<T>::encode(Writer&, const T&) -> Core::Status` 写一个值；
`Codec<T>::decode(const Reader&) -> Core::Result<T>` 返回新的候选值。内建支持 bool、64-bit 范围内整数、
浮点、string、optional、vector、array、`map<string,T>` 与 ObjectId。`string_view` 只支持 encode，
避免返回指向已释放 JSON 的 borrowed string。枚举/领域值由游戏显式 Codec 校验合法集合。

```cpp
#include <tina/serialization/TypeRegistry.hpp>

struct Actor { virtual ~Actor() = default; };
struct Enemy final : Actor {
    std::string name;
    Tina::Core::i32 hitPoints = 0;
};

template<> struct Tina::Serialization::Codec<Enemy> {
    static Tina::Core::Status encode(Tina::Serialization::Writer& writer, const Enemy& enemy) {
        return writer.object([&](Tina::Serialization::ObjectWriter& object) {
            if (auto status = object.field("name", enemy.name); !status) return status;
            return object.field("hitPoints", enemy.hitPoints);
        });
    }
    static Tina::Core::Result<Enemy> decode(const Tina::Serialization::Reader& reader) {
        if (auto status = reader.fieldsOnly({"name", "hitPoints"}); !status)
            return Tina::Core::failure(status.error());
        auto name = reader.field<std::string>("name");
        if (!name) return Tina::Core::failure(name.error());
        auto hp = reader.field<Tina::Core::i32>("hitPoints");
        if (!hp) return Tina::Core::failure(hp.error());
        if (*hp < 0) return Tina::Core::failure(reader.error(
            Tina::Serialization::ErrorCode::InvalidData, "Negative hitPoints"));
        Enemy candidate;
        candidate.name = std::move(*name);
        candidate.hitPoints = *hp;
        return candidate;
    }
};
```

`field<T>()` 要求字段存在，类型/整数范围必须正确；`optionalField<T>()` 把缺失/null 映射为空。
`fieldsOnly()` 显式拒绝未知字段。Reader 拥有不可变 JSON node，不借用源 byte buffer；错误包含字段路径。
`members()` 按源顺序线性枚举拥有名称与子 Reader 的成员；`map<string,T>` 使用该路径，
不对每个 key 重新扫描对象，避免大型存档字典退化为平方复杂度。
直接从 `Core::JsonValue` 创建 Reader 时，调用者应先以受限 `JsonDocument::parse` 得到该值。

Writer 的 object/array 回调只能在调用期间使用借用 writer；不能保存引用或跨线程写它。
Writer 记录首个失败，即使上层忽略一次失败再返回 success，也不会发布半份 archive。
空 codec、一个位置写多个值、重复字段、NaN/Inf、非法 UTF-8 和 callback exception 都失败。

## 多态注册与对象关系

```cpp
Tina::Serialization::TypeRegistry<Actor> registry;
auto registration = registry.registerType<Enemy>("game.enemy");
// 必须检查 registration；注册只在 owner 启动阶段进行。
```

注册 ID 为 1..128 bytes 的稳定 ASCII 标识，允许字母、数字和 `._-:/`。重复 ID / C++ 类型均拒绝。
默认最多256种类型，可在构造 registry 时明确调整。RTTI 仅用于内存里的动态类型匹配，
不持久化 `typeid().name()`、C++ 类名或 hash；wire 为 null 或 `{"type":"game.enemy","data":{...}}`。
未知 ID 不执行 factory；factory 的异常、null 结果或错误动态类型失败。无默认构造路径的类型可以
向 `registerType<Derived>` 注入返回 `Result<unique_ptr<Derived>>` 的 factory。

`ObjectTable<Base>` 唯一拥有 `map<ObjectId, unique_ptr<Base>>`。ObjectId 是显式持久 ID，0 仅表示空引用；
非零 ID 唯一，不能使用地址、EntityId generation 或临时 registry handle。

```cpp
auto bytes = Tina::Serialization::encodeJsonWith([&](Tina::Serialization::Writer& writer) {
    return table.encode(writer, registry);
});
if (!bytes) return Tina::Core::failure(bytes.error());

auto root = Tina::Serialization::decodeJson(*bytes);
if (!root) return Tina::Core::failure(root.error());
auto candidate = Tina::Serialization::ObjectTable<Actor>::decode(*root, registry);
if (!candidate) return Tina::Core::failure(candidate.error());
// 检查每个对象中的 ObjectId：candidate->resolve(id)。不存在的非零 ID 返回 UnresolvedReference。
// 全部引用及游戏不变量通过后，才 move candidate 到 live state。
```

先建立所有对象，再解析关系，能表达前向引用/环；引擎不自动序列化任意裸指针图。
decode 失败由 RAII 销毁全部候选对象，不改 live state。factory/codec 应只创建数据，不直接修改 live World
或触发 IO；引擎不能回滚用户 callback 自己产生的外部副作用。const registry 可共享查询，但 factory
捕获的可变资源仍需游戏自行同步。

## SaveStore 接入

```cpp
auto encoded = Tina::Serialization::encodeJson(snapshot);
if (!encoded) return Tina::Core::failure(encoded.error());
auto saved = store.saveSlot({.slot = {0}, .dataVersion = 1,
    .displayName = "冒险进度", .payload = *encoded});
if (!saved) return Tina::Core::failure(saved.error());

auto loaded = store.loadSlot({0});
if (!loaded) return Tina::Core::failure(loaded.error());
// 先验证 loaded->metadata.dataVersion，再 decodeJson<Snapshot>(loaded->payload)。
```

上面片段中的 `snapshot`、`Snapshot`、`store` 是游戏自己的 DTO/owner。异步保存使用同一 encoded bytes
调用 `beginSave`（提交时复制），不要让 IO worker 遍历正被游戏线程修改的对象。

## 预算与验证

Archive 默认 maxBytes=16 MiB、maxDepth=128（根深度0，含 scalar leaf）、maxNodes=1,000,000；
byte budget 包含完整 envelope。计数与输入长度在处理时检查，输出编码后再检查实际 JSON 字节数。
**这不是进程峰值内存上限**：JSON DOM、DTO、字符串转义结果与输出 buffer 各自需要空间。
Core 统一拒绝重复 key；整数 decode 不截断浮点、不溢出回绕。文本源码/JSON 是 UTF-8，MSVC 保持 `/utf-8`；
中文 metadata 和字段不经过 ANSI/GBK 中转。

回归面包括 scalar/optional/map/array、64-bit 极值、字段路径、预算、重复 key、异常/sticky writer、
注册冲突/未知 ID/错误 factory、对象引用及候选销毁、SaveStore 字节往返。测试程序须获得运行授权后执行；
源码存在与编译成功不等于运行通过。
