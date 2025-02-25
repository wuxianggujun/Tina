#include "tina/core/Scene.hpp"
#include "tina/core/Node.hpp"

namespace Tina {

Scene::Scene(const std::string& name) : m_name(name) {}

Scene::~Scene() {
    // 清理所有节点
    m_nodes.clear();
}

void Scene::onEnter() {
    // 场景进入时的初始化逻辑
}

void Scene::onExit() {
    // 场景退出时的清理逻辑
}

void Scene::update(float deltaTime) {
    // 更新场景中的所有节点
    for (auto& node : m_nodes) {
        if (node) {
            node->update(deltaTime);
        }
    }
}

void Scene::render() {
    // 渲染场景中的所有节点
    for (auto& node : m_nodes) {
        if (node) {
            node->render();
        }
    }
}

Node* Scene::createNode(const std::string& name) {
    auto node = MakeUnique<Node>(name);
    Node* nodePtr = node.get();
    m_nodes.push_back(std::move(node));
    return nodePtr;
}

void Scene::destroyNode(Node* node) {
    if (!node) return;
    
    // 查找并移除节点
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(),
        [node](const UniquePtr<Node>& ptr) { return ptr.get() == node; });
    
    if (it != m_nodes.end()) {
        m_nodes.erase(it);
    }
}

} // namespace Tina