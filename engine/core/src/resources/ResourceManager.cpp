#include "tina/resources/ResourceManager.hpp"
#include "tina/resources/FileWatcher.hpp"
#include "tina/log/Logger.hpp"

namespace Tina {

void ResourceManager::startFileWatcher() {
    TINA_PROFILE_FUNCTION();
    
    try {
        FileWatcher::getInstance().start();
        TINA_LOG_INFO("Started file watcher");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to start file watcher: {}", e.what());
        m_HotReloadEnabled = false;
    }
}

void ResourceManager::stopFileWatcher() {
    TINA_PROFILE_FUNCTION();
    
    try {
        FileWatcher::getInstance().stop();
        TINA_LOG_INFO("Stopped file watcher");
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to stop file watcher: {}", e.what());
    }
}

void ResourceManager::addFileWatch(const std::string& path, std::function<void()> callback) {
    TINA_PROFILE_FUNCTION();
    
    try {
        FileWatcher::getInstance().addWatch(path, [this, path, callback]() {
            TINA_LOG_INFO("Detected change in resource file: {}", path);
            try {
                callback();
                TINA_LOG_INFO("Successfully reloaded resource: {}", path);
            }
            catch (const std::exception& e) {
                TINA_LOG_ERROR("Failed to reload resource {}: {}", path, e.what());
            }
        });
        TINA_LOG_DEBUG("Added file watch for: {}", path);
    }
    catch (const std::exception& e) {
        TINA_LOG_ERROR("Failed to add file watch for {}: {}", path, e.what());
    }
}

} // namespace Tina 