#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include "tina/utils/Profiler.hpp"

namespace Tina {

// 资源包中的资源项
struct PackageEntry {
    std::string path;           // 资源相对路径
    uint64_t offset;           // 在包文件中的偏移
    uint64_t size;            // 资源大小
    uint64_t compressedSize;  // 压缩后的大小
    uint32_t flags;           // 标志位(压缩、加密等)
    std::string type;         // 资源类型
    std::string hash;         // 资源哈希值
};

// 资源包头部
struct PackageHeader {
    char magic[4] = {'T', 'P', 'K', 'G'};  // 魔数
    uint32_t version = 1;                   // 版本号
    uint32_t flags = 0;                     // 标志位
    uint64_t entryCount = 0;               // 资源项数量
    uint64_t contentOffset = 0;            // 内容起始偏移
};

// 资源包类
class ResourcePackage {
public:
    // 创建空包
    static std::shared_ptr<ResourcePackage> create(const std::string& path);
    
    // 加载已有包
    static std::shared_ptr<ResourcePackage> load(const std::string& path);

    // 添加资源
    bool addResource(const std::string& path, const std::string& type);
    
    // 提取资源
    std::vector<uint8_t> extractResource(const std::string& path);
    
    // 保存包
    bool save();
    
    // 获取资源项信息
    const PackageEntry* getEntry(const std::string& path) const;
    
    // 获取所有资源项
    const std::unordered_map<std::string, PackageEntry>& getEntries() const {
        return m_Entries;
    }

    // 获取包路径
    const std::string& getPath() const { return m_Path; }

private:
    ResourcePackage(const std::string& path);

    bool writeHeader();
    bool writeEntries();
    bool writeContent();
    bool readHeader();
    bool readEntries();

    std::string calculateHash(const std::vector<uint8_t>& data);
    std::vector<uint8_t> compressData(const std::vector<uint8_t>& data);
    std::vector<uint8_t> decompressData(const std::vector<uint8_t>& data, size_t originalSize);

private:
    std::string m_Path;
    PackageHeader m_Header;
    std::unordered_map<std::string, PackageEntry> m_Entries;
    std::unordered_map<std::string, std::vector<uint8_t>> m_PendingData;
};

// 资源包管理器
class ResourcePackageManager {
public:
    static ResourcePackageManager& getInstance() {
        static ResourcePackageManager instance;
        return instance;
    }

    // 加载资源包
    bool loadPackage(const std::string& path);
    
    // 卸载资源包
    void unloadPackage(const std::string& path);
    
    // 从包中加载资源
    std::vector<uint8_t> loadResource(const std::string& path);
    
    // 获取资源信息
    const PackageEntry* getResourceInfo(const std::string& path) const;
    
    // 检查资源是否存在于任何包中
    bool hasResource(const std::string& path) const;

private:
    ResourcePackageManager() = default;
    ~ResourcePackageManager() = default;

    ResourcePackageManager(const ResourcePackageManager&) = delete;
    ResourcePackageManager& operator=(const ResourcePackageManager&) = delete;

private:
    std::unordered_map<std::string, std::shared_ptr<ResourcePackage>> m_Packages;
    std::unordered_map<std::string, std::weak_ptr<ResourcePackage>> m_ResourceToPackage;
};

} // namespace Tina 