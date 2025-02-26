////
//// Created by wuxianggujun on 2025/2/26.
////
//
//#ifndef VIEW_HPP
//#define VIEW_HPP
//
//namespace Tina {
//
//    class View {
//    public:
//        View(uint8_t viewId);
//        ~View();
//
//        void setViewport(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
//        void setOrthographicProjection(float left, float right, float bottom, float top);
//    
//        uint8_t getViewId() const { return m_viewId; }
//    
//        // 添加节点管理功能
//        void addNode(Node* node);
//        void removeNode(Node* node);
//    
//        void update(float deltaTime);
//        void render();
//
//    private:
//        uint8_t m_viewId;
//        std::vector<Node*> m_nodes;
//    
//        // 视图和投影矩阵
//        float m_view[16];
//        float m_proj[16];
//    };
//} // Tina
//
//#endif //VIEW_HPP
