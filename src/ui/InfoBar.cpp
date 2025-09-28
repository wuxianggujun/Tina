//
// Info Bar Implementation
//

#include "InfoBar.hpp"
#include "../core/Log.hpp"

namespace Tina::UI {

InfoBar::InfoBar(UISystem* uiSystem) : m_uiSystem(uiSystem) {}

InfoBar::~InfoBar() {
    shutdown();
}

bool InfoBar::initialize() {
    if (!m_uiSystem || !m_uiSystem->getContext()) {
        TINA_ERROR("UISystem not initialized");
        return false;
    }
    
    // 暂时简化实现，先不使用数据绑定
    // TODO: 在RmlUI稳定后添加数据绑定功能
    
    // Load the InfoBar document from inline RML
    const char* infoBarRML = R"(
<rml>
<head>
    <title>Info Bar</title>
    <style>
        body {
            font-family: 'Source Han Sans SC', 'Noto Sans SC', sans-serif;
            font-size: 14px;
            margin: 0;
            padding: 0;
            background: rgba(0, 0, 0, 0.7);
            color: white;
        }
        
        .info-bar {
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            height: 40px;
            background: linear-gradient(to bottom, rgba(0, 0, 0, 0.8), rgba(0, 0, 0, 0.6));
            border-bottom: 1px solid rgba(255, 255, 255, 0.2);
            padding: 8px 16px;
            display: flex;
            align-items: center;
            z-index: 1000;
        }
        
        .info-section {
            margin-right: 24px;
            display: flex;
            align-items: center;
        }
        
        .info-label {
            color: rgba(255, 255, 255, 0.7);
            margin-right: 4px;
        }
        
        .info-value {
            color: white;
            font-weight: bold;
        }
        
        .separator {
            width: 1px;
            height: 20px;
            background: rgba(255, 255, 255, 0.2);
            margin: 0 16px;
        }
        
        .fps {
            color: #4CAF50;
        }
        
        .camera-info {
            color: #2196F3;
        }
        
        .debug-info {
            color: #FF9800;
        }
        
        .map-info {
            color: #9C27B0;
        }
    </style>
</head>
<body>
    <div class="info-bar">
        <div class="info-section map-info">
            <span class="info-label">地图:</span>
            <span class="info-value" id="map_width">0</span>
            <span class="info-label">x</span>
            <span class="info-value" id="map_height">0</span>
        </div>
        
        <div class="separator"></div>
        
        <div class="info-section camera-info">
            <span class="info-label">相机:</span>
            <span class="info-value" id="camera_x">0.0</span>
            <span class="info-label">,</span>
            <span class="info-value" id="camera_y">0.0</span>
            <span class="info-label">缩放:</span>
            <span class="info-value" id="camera_zoom">1.0</span>
        </div>
        
        <div class="separator"></div>
        
        <div class="info-section fps">
            <span class="info-label">FPS:</span>
            <span class="info-value" id="fps">0.0</span>
        </div>
        
        <div class="separator" id="debug_separator" style="display: none;"></div>
        
        <div class="info-section debug-info" id="debug_info" style="display: none;">
            <span class="info-label">区块:</span>
            <span class="info-value" id="chunk_count">0</span>
            <span class="info-label">碎片:</span>
            <span class="info-value" id="debris_count">0</span>
        </div>
    </div>
</body>
</rml>
)";
    
    m_document = m_uiSystem->getContext()->LoadDocumentFromMemory(infoBarRML);
    if (!m_document) {
        TINA_ERROR("Failed to load InfoBar document");
        return false;
    }
    
    m_document->Show();
    
    TINA_INFO("InfoBar initialized successfully");
    return true;
}

void InfoBar::shutdown() {
    if (m_document) {
        m_document->Close();
        m_document = nullptr;
    }
}

void InfoBar::update(const InfoBarData& data) {
    m_data = data;
    updateDataModel();
}

void InfoBar::setVisible(bool visible) {
    m_visible = visible;
    if (m_document) {
        if (visible) {
            m_document->Show();
        } else {
            m_document->Hide();
        }
    }
}

bool InfoBar::isVisible() const {
    return m_visible;
}

void InfoBar::toggleDebugInfo() {
    m_data.showDebugInfo = !m_data.showDebugInfo;
    updateDataModel();
}

void InfoBar::updateDataModel() {
    if (!m_document) return;
    
    // 手动更新UI元素的内容
    auto updateElement = [this](const char* id, const Rml::String& text) {
        if (auto element = m_document->GetElementById(id)) {
            element->SetInnerRML(text);
        }
    };
    
    // 更新地图信息
    updateElement("map_width", Rml::CreateString("%d", m_data.mapWidth));
    updateElement("map_height", Rml::CreateString("%d", m_data.mapHeight));
    
    // 更新相机信息
    updateElement("camera_x", Rml::CreateString("%.1f", m_data.cameraX));
    updateElement("camera_y", Rml::CreateString("%.1f", m_data.cameraY));
    updateElement("camera_zoom", Rml::CreateString("%.2f", m_data.cameraZoom));
    
    // 更新FPS
    updateElement("fps", Rml::CreateString("%.1f", m_data.fps));
    
    // 更新调试信息显示状态
    if (auto debugInfo = m_document->GetElementById("debug_info")) {
        debugInfo->SetProperty("display", m_data.showDebugInfo ? "flex" : "none");
    }
    if (auto debugSeparator = m_document->GetElementById("debug_separator")) {
        debugSeparator->SetProperty("display", m_data.showDebugInfo ? "block" : "none");
    }
    
    // 如果显示调试信息，更新数值
    if (m_data.showDebugInfo) {
        updateElement("chunk_count", Rml::CreateString("%d", m_data.chunkCount));
        updateElement("debris_count", Rml::CreateString("%d", m_data.debrisCount));
    }
}

} // namespace Tina::UI
