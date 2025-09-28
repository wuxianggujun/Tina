//
// Info Bar Component for displaying game information
//

#pragma once

#include "UISystem.hpp"
#include <RmlUi/Core.h>

namespace Tina::UI {

struct InfoBarData {
    int mapWidth = 0;
    int mapHeight = 0;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraZoom = 1.0f;
    float fps = 0.0f;
    int chunkCount = 0;
    int debrisCount = 0;
    bool showDebugInfo = false;
};

class InfoBar {
public:
    explicit InfoBar(UISystem* uiSystem);
    ~InfoBar();

    bool initialize();
    void shutdown();
    
    void update(const InfoBarData& data);
    void setVisible(bool visible);
    bool isVisible() const;
    
    void toggleDebugInfo();

private:
    void updateDataModel();
    
private:
    UISystem* m_uiSystem = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    InfoBarData m_data;
    bool m_visible = true;
};

} // namespace Tina::UI