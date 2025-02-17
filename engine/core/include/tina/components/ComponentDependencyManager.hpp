#pragma once

#include <unordered_map>
#include <unordered_set>
#include <typeindex>
#include <vector>
#include <string>
#include <entt/entt.hpp>
#include "tina/log/Logger.hpp"

namespace Tina {

class ComponentDependencyManager {
public:
    static ComponentDependencyManager& getInstance() {
        static ComponentDependencyManager instance;
        return instance;
    }

    // 注册组件依赖关系
    template<typename T, typename Dependency>
    void registerDependency() {
        std::type_index componentType(typeid(T));
        std::type_index dependencyType(typeid(Dependency));
        
        m_Dependencies[componentType].insert(dependencyType);
        m_DependentComponents[dependencyType].insert(componentType);

        TINA_CORE_LOG_DEBUG("Registered dependency: {} depends on {}", 
            typeid(T).name(), typeid(Dependency).name());
    }

    // 注册多个依赖
    template<typename T, typename... Dependencies>
    void registerDependencies() {
        (registerDependency<T, Dependencies>(), ...);
    }

    // 检查是否存在循环依赖
    bool hasCircularDependency(const std::type_index& componentType,
                             std::unordered_set<std::type_index>& visited,
                             std::unordered_set<std::type_index>& recursionStack) {
        visited.insert(componentType);
        recursionStack.insert(componentType);

        auto it = m_Dependencies.find(componentType);
        if (it != m_Dependencies.end()) {
            for (const auto& dependency : it->second) {
                if (visited.find(dependency) == visited.end()) {
                    if (hasCircularDependency(dependency, visited, recursionStack)) {
                        return true;
                    }
                }
                else if (recursionStack.find(dependency) != recursionStack.end()) {
                    return true;
                }
            }
        }

        recursionStack.erase(componentType);
        return false;
    }

    // 获取组件的所有依赖
    template<typename T>
    std::vector<std::type_index> getDependencies() const {
        std::type_index componentType(typeid(T));
        auto it = m_Dependencies.find(componentType);
        if (it != m_Dependencies.end()) {
            return std::vector<std::type_index>(it->second.begin(), it->second.end());
        }
        return {};
    }

    // 获取依赖于指定组件的所有组件
    template<typename T>
    std::vector<std::type_index> getDependentComponents() const {
        std::type_index componentType(typeid(T));
        auto it = m_DependentComponents.find(componentType);
        if (it != m_DependentComponents.end()) {
            return std::vector<std::type_index>(it->second.begin(), it->second.end());
        }
        return {};
    }

    // 检查组件是否满足其依赖要求
    template<typename T>
    bool checkDependencies(const entt::registry& registry, entt::entity entity) const {
        std::type_index componentType(typeid(T));
        auto it = m_Dependencies.find(componentType);
        if (it != m_Dependencies.end()) {
            for (const auto& dependency : it->second) {
                if (!registry.any_of<T>(entity)) {
                    TINA_CORE_LOG_WARN("Entity {} missing required dependency {} for component {}",
                        static_cast<uint32_t>(entity),
                        dependency.name(),
                        componentType.name());
                    return false;
                }
            }
        }
        return true;
    }

    // 获取组件的拓扑排序
    std::vector<std::type_index> getTopologicalOrder() const {
        std::vector<std::type_index> result;
        std::unordered_set<std::type_index> visited;

        // 对每个组件执行深度优先搜索
        for (const auto& pair : m_Dependencies) {
            if (visited.find(pair.first) == visited.end()) {
                topologicalSortUtil(pair.first, visited, result);
            }
        }

        // 反转结果以获得正确的顺序
        std::reverse(result.begin(), result.end());
        return result;
    }

private:
    void topologicalSortUtil(const std::type_index& componentType,
                            std::unordered_set<std::type_index>& visited,
                            std::vector<std::type_index>& result) const {
        visited.insert(componentType);

        auto it = m_Dependencies.find(componentType);
        if (it != m_Dependencies.end()) {
            for (const auto& dependency : it->second) {
                if (visited.find(dependency) == visited.end()) {
                    topologicalSortUtil(dependency, visited, result);
                }
            }
        }

        result.push_back(componentType);
    }

private:
    ComponentDependencyManager() = default;
    ~ComponentDependencyManager() = default;
    
    ComponentDependencyManager(const ComponentDependencyManager&) = delete;
    ComponentDependencyManager& operator=(const ComponentDependencyManager&) = delete;

private:
    // 组件类型到其依赖的映射
    std::unordered_map<std::type_index, std::unordered_set<std::type_index>> m_Dependencies;
    
    // 组件类型到依赖于它的组件的映射
    std::unordered_map<std::type_index, std::unordered_set<std::type_index>> m_DependentComponents;
};

} // namespace Tina 