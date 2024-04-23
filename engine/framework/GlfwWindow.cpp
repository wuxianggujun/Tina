#include "GlfwWindow.hpp"

namespace Tina {

    GLFWwindow* GlfwWindow::window = nullptr;

    bool GlfwWindow::initialize(const char* title, uint16_t width, uint16_t height, const char* iconFilePath, bool useFullScreen)
    {
        //设置错误回调显示报错信息
        glfwSetErrorCallback(GlfwUtils::glfwLogError);
        if (!glfwInit())
        {
            throw std::runtime_error("Cannot initialize GLFW.");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        //glfwWindowHint(GLFW_RESIZABLE, useFullScreen ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

        // Create window,make opengl context current request v-sync
        this->window = glfwCreateWindow(width, height, title, nullptr, nullptr);
        if (!this->window)
        {
            glfwTerminate();
            throw std::runtime_error("Can not create this window");
        }

        glfwMakeContextCurrent(this->window);
        glfwSetWindowUserPointer(this->window, this);
        glfwSwapInterval(1);


        // 在主线程进行渲染，不另开子线程
        bgfx::renderFrame();

        bgfx::PlatformData platFormData;
        platFormData.nwh = GlfwUtils::getNativeWindowHandle(this->window);
#if BX_PLATFORM_LINUX
        platFormData.ndt = glfwGetX11Display();
#endif

        bgfx::Init bgfxInit;
        bgfxInit.platformData = platFormData;
        // 自动选择适合平台的默认呈现后端
        bgfxInit.type = bgfx::RendererType::Count;

        // TODO
        bgfxInit.resolution.width = width;
        bgfxInit.resolution.height = height;
        bgfxInit.resolution.reset = BGFX_RESET_VSYNC;
        bgfxInit.debug = false;
        bgfxInit.profile = false;

        if (!bgfx::init(bgfxInit))
        {
            LOG_ERROR("Bgfx Init error!");
            throw std::runtime_error("Bgfx Init error!");
        }
        return true;
    }

    GlfwWindow::~GlfwWindow()
    {
        close();
    }

 

    void GlfwWindow::setTitle(const char* title) {
        glfwSetWindowTitle(this->window, title);
    }

    void GlfwWindow::setFullScreen(bool flag) {

    }

    void GlfwWindow::setResizeable(bool flag)
    {
    }

    void GlfwWindow::setSize(uint16_t width, uint16_t height) {
        glfwSetWindowSize(this->window,static_cast<int>(width),static_cast<int>(height));
    }

    void GlfwWindow::setWindowIcon(const char* iconFilePath) const {

    }

    void GlfwWindow::setMouseVisible(bool isVisible, uint32_t xMouse, uint32_t yMouse) {

    }


    void GlfwWindow::update() {
            /* Swap front and back buffers */
 //           glfwSwapBuffers(this->window);

            /* Poll for and process events */
            glfwPollEvents();
    }


    void GlfwWindow::close() {
        bgfx::shutdown();
        if (window)
        {
            glfwDestroyWindow(window);
        }
        glfwTerminate();
    }

}
