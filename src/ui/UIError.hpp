//
// UIError - UI渲染器错误处理系统
// 职责：提供结构化的错误报告和处理机制
//

#pragma once

#include <string>
#include <chrono>
#include "../core/Log.hpp"

namespace Tina::UI {

// 错误代码枚举
enum class UIErrorCode {
    None = 0,
    ShaderInvalid,              // 着色器无效
    BufferAllocationFailed,     // 缓冲区分配失败
    TextureInvalid,             // 纹理无效
    TextRendererMissing,        // 文本渲染器缺失
    BatchOverflow,              // 批次溢出
    InvalidParameter,           // 参数无效
    TransientBufferFull         // 瞬态缓冲区满
};

// 错误信息结构
struct UIError {
    UIErrorCode code = UIErrorCode::None;
    std::string message;
    std::string location;  // 函数名或位置
    std::chrono::system_clock::time_point timestamp;
    uint32_t droppedItems = 0;  // 因错误丢弃的绘制项数

    // 清空错误
    void clear() {
        code = UIErrorCode::None;
        message.clear();
        location.clear();
        droppedItems = 0;
    }

    // 是否有错误
    bool hasError() const {
        return code != UIErrorCode::None;
    }

    // 设置错误
    void setError(UIErrorCode errorCode, const std::string& errorMessage,
                  const std::string& errorLocation = "") {
        code = errorCode;
        message = errorMessage;
        location = errorLocation;
        timestamp = std::chrono::system_clock::now();
    }

    // 转换为字符串
    std::string toString() const {
        if (!hasError()) return "No error";

        std::string result = "[" + location + "] Error " +
                           std::to_string(static_cast<int>(code)) +
                           ": " + message;

        if (droppedItems > 0) {
            result += " (dropped " + std::to_string(droppedItems) + " items)";
        }

        return result;
    }

    // 输出到日志
    void logError() const {
        if (hasError()) {
            TINA_ERROR("UIRenderer: {}", toString());
        }
    }
};

// 错误处理器接口
class IUIErrorHandler {
public:
    virtual ~IUIErrorHandler() = default;
    virtual void onError(const UIError& error) = 0;
};

// 默认错误处理器（输出到日志）
class DefaultUIErrorHandler : public IUIErrorHandler {
public:
    void onError(const UIError& error) override {
        error.logError();
    }
};

} // namespace Tina::UI