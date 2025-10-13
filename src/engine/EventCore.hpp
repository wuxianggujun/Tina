//
// EventCore.hpp - 事件系统核心定义
// 职责：定义事件基类、类型枚举、配置常量
// 设计：基于 CRTP + 手动枚举 TypeID
//

#pragma once

#include "../core/Core.hpp"
#include "../core/Hash.hpp"
#include <cstdint>
#include <chrono>
#include <typeinfo>
#include <cstdio>

namespace Tina::Engine {

// ==================== 配置常量 ====================

// 事件队列容量（2^12 = 4096，缓存友好）
constexpr size_t EVENT_QUEUE_CAPACITY = 4096;

// 对象池容量（每类型）
constexpr size_t EVENT_POOL_CAPACITY = 2048;

// 事件处理器函数大小（字节）
constexpr size_t HANDLER_FUNCTION_SIZE = 64;

// 优先级队列配额（高/中/低）
constexpr size_t PRIORITY_QUOTA_HIGH = 1000;
constexpr size_t PRIORITY_QUOTA_MEDIUM = 200;
constexpr size_t PRIORITY_QUOTA_LOW = 50;

// ==================== 事件类型枚举 ====================

// 事件类型 ID（编译期常量，用于数组索引）
enum class EventTypeId : uint32_t {
    None = 0,

    // ========== 系统事件（0-31） ==========
    WindowResized,
    WindowClosed,
    WindowFocused,
    WindowUnfocused,
    WindowMinimized,
    WindowMaximized,
    WindowRestored,
    WindowMoved,

    // ========== 输入事件（32-95） ==========
    KeyPressed,
    KeyReleased,
    TextInput,

    MouseMoved,
    MouseButtonPressed,
    MouseButtonReleased,
    MouseWheel,

    DropFile,

    // ========== 游戏事件（96-191） ==========
    // 注意：这里只保留通用的游戏事件，具体游戏逻辑事件应使用 CustomEvent
    
    // 游戏生命周期
    GamePaused,
    GameResumed,
    GameOver,
    
    LevelLoaded,
    LevelUnloaded,

    // ========== UI 事件（192-255） ==========
    UIButtonClicked,
    UIHoverEnter,
    UIHoverLeave,
    UIFocusGained,
    UIFocusLost,
    UITextChanged,

    // ========== 自定义扩展事件（256+） ==========
    Custom = 256,  // 用户自定义事件起始ID
    CustomEnd = 512,  // 自定义事件结束ID（留256个位置）

    // 自动计数（必须放在最后）
    MaxEventTypes = 1024  // 扩大事件类型数量上限
};

// ==================== 事件优先级 ====================

enum class EventPriority : uint8_t {
    High = 0,    // 系统关键事件（窗口关闭、崩溃）
    Medium = 1,  // 游戏逻辑事件（玩家移动、攻击）
    Low = 2      // UI 事件（按钮点击、hover）
};

// ==================== CRTP 事件基类 ====================

// 事件基类（使用 CRTP 实现静态多态）
// 注意：不使用虚函数，保持POD特性以提高性能
template<typename Derived, EventTypeId TypeId>
struct Event {
    // 编译期类型 ID
    static constexpr EventTypeId TYPE_ID = TypeId;

    // 运行时数据
    uint32_t timestamp = 0;         // 时间戳（毫秒）
    EventPriority priority = EventPriority::Medium;  // 优先级

    // 获取类型 ID（方便运行时查询）
    EventTypeId getTypeId() const { return TYPE_ID; }

    // 不使用虚析构函数，保持trivially destructible
    // CRTP模式不需要虚函数，完全使用静态多态
};

// ==================== 自定义事件基类（游戏层扩展） ====================

// 自定义事件模板（运行时生成唯一 ID，无需修改引擎代码）
// 用法：struct MyEvent : public CustomEvent<MyEvent> { ... };
template<typename Derived>
struct CustomEvent {
    // 🎯 使用运行时类型哈希自动生成唯一 ID
    // 注意：ID 在首次访问时计算并缓存
    static EventTypeId TYPE_ID;

    // 运行时数据
    uint32_t timestamp = 0;
    EventPriority priority = EventPriority::Medium;

    // 获取类型 ID
    EventTypeId getTypeId() const { return TYPE_ID; }
    
    // 初始化类型 ID（静态初始化）
    static EventTypeId initTypeId() {
        const char* name = typeid(Derived).name();
        Core::Hash::Hash64 hash = Core::Hash::String64(name);
        
        // 🔧 改进哈希分布：混合高位和低位，减少冲突
        // CustomEnd - Custom = 512 - 256 = 256 个可用位置
        constexpr uint32_t CUSTOM_RANGE = 256;
        uint32_t hash32 = static_cast<uint32_t>(hash ^ (hash >> 32));  // 混合高低位
        uint32_t offset = hash32 % CUSTOM_RANGE;
        uint32_t typeId = static_cast<uint32_t>(EventTypeId::Custom) + offset;
        
        return static_cast<EventTypeId>(typeId);
    }
};

// 静态成员定义
template<typename Derived>
EventTypeId CustomEvent<Derived>::TYPE_ID = CustomEvent<Derived>::initTypeId();

// ==================== 宏简化事件定义 ====================

// 定义事件的便捷宏
#define TINA_EVENT(ClassName, TypeIdValue) \
    struct ClassName : ::Tina::Engine::Event<ClassName, ::Tina::Engine::EventTypeId::TypeIdValue>

// 定义带优先级的事件
#define TINA_EVENT_PRIORITY(ClassName, TypeIdValue, PriorityValue) \
    struct ClassName : ::Tina::Engine::Event<ClassName, ::Tina::Engine::EventTypeId::TypeIdValue> { \
        ClassName() { this->priority = ::Tina::Engine::EventPriority::PriorityValue; } \
    }

// ==================== 工具函数 ====================

// 将事件类型 ID 转换为字符串（用于调试）
inline const char* eventTypeIdToString(EventTypeId id) {
    switch (id) {
        case EventTypeId::None: return "None";

        // 系统事件
        case EventTypeId::WindowResized: return "WindowResized";
        case EventTypeId::WindowClosed: return "WindowClosed";
        case EventTypeId::WindowFocused: return "WindowFocused";
        case EventTypeId::WindowUnfocused: return "WindowUnfocused";
        case EventTypeId::WindowMinimized: return "WindowMinimized";
        case EventTypeId::WindowMaximized: return "WindowMaximized";
        case EventTypeId::WindowRestored: return "WindowRestored";
        case EventTypeId::WindowMoved: return "WindowMoved";

        // 输入事件
        case EventTypeId::KeyPressed: return "KeyPressed";
        case EventTypeId::KeyReleased: return "KeyReleased";
        case EventTypeId::TextInput: return "TextInput";
        case EventTypeId::MouseMoved: return "MouseMoved";
        case EventTypeId::MouseButtonPressed: return "MouseButtonPressed";
        case EventTypeId::MouseButtonReleased: return "MouseButtonReleased";
        case EventTypeId::MouseWheel: return "MouseWheel";
        case EventTypeId::DropFile: return "DropFile";

        // 游戏事件
        case EventTypeId::GamePaused: return "GamePaused";
        case EventTypeId::GameResumed: return "GameResumed";
        case EventTypeId::GameOver: return "GameOver";
        case EventTypeId::LevelLoaded: return "LevelLoaded";
        case EventTypeId::LevelUnloaded: return "LevelUnloaded";

        // UI 事件
        case EventTypeId::UIButtonClicked: return "UIButtonClicked";
        case EventTypeId::UIHoverEnter: return "UIHoverEnter";
        case EventTypeId::UIHoverLeave: return "UIHoverLeave";
        case EventTypeId::UIFocusGained: return "UIFocusGained";
        case EventTypeId::UIFocusLost: return "UIFocusLost";
        case EventTypeId::UITextChanged: return "UITextChanged";

        default: return "Unknown";
    }
}

// 获取当前时间戳（毫秒）
inline uint64_t getCurrentTimeMs() {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<uint64_t>(duration.count());
}

// ==================== 自定义事件辅助宏 ====================

/**
 * 定义自定义事件的宏（自动分配ID）
 * 
 * 使用示例：
 *   TINA_CUSTOM_EVENT(MyCustomEvent) {
 *       int data;
 *       float value;
 *   };
 * 
 * 注意：
 * - 每个自定义事件会自动获得唯一的 TYPE_ID
 * - 基于类型哈希，避免冲突
 * - 仅支持 POD 类型（trivially copyable + trivially destructible）
 */
#define TINA_CUSTOM_EVENT(ClassName) \
    struct ClassName { \
        static constexpr ::Tina::Engine::EventTypeId TYPE_ID = \
            static_cast<::Tina::Engine::EventTypeId>( \
                ::Tina::Engine::EventTypeId::Custom + \
                (typeid(ClassName).hash_code() % 256) \
            ); \
        uint32_t timestamp = 0; \
        ::Tina::Engine::EventPriority priority = ::Tina::Engine::EventPriority::Medium; \
        ::Tina::Engine::EventTypeId getTypeId() const { return TYPE_ID; } \
    }; \
    static_assert(std::is_trivially_copyable_v<ClassName>, #ClassName " must be trivially copyable"); \
    static_assert(std::is_trivially_destructible_v<ClassName>, #ClassName " must be trivially destructible")

} // namespace Tina::Engine
