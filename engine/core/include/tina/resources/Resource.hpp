#pragma once

#include "tina/core/Core.hpp"
#include <string>

namespace Tina {

class TINA_CORE_API Resource {
public:
    virtual ~Resource() = default;
    
    // 检查资源是否有效
    virtual bool isValid() const = 0;
    
    // 重新加载资源
    virtual bool reload() = 0;
    
    // 释放资源
    virtual void release() = 0;
    
    // 获取资源路径
    const std::string& getPath() const { return m_Path; }
    void setPath(const std::string& path) { m_Path = path; }

protected:
    std::string m_Path;
};

} // namespace Tina
