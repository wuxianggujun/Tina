#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/uint32_t.h>
#include <bx/constants.h>
#include <GLFW/glfw3.h>

#if BX_PLATFORM_LINUX
#	if TINA_CONFIG_USE_WAYLAND
#		include <wayland-egl.h>
#		define GLFW_EXPOSE_NATIVE_WAYLAND
#	else
#		define GLFW_EXPOSE_NATIVE_X11
#		define GLFW_EXPOSE_NATIVE_GLX
#	endif
#elif BX_PLATFORM_OSX
#	define GLFW_EXPOSE_NATIVE_COCOA
#	define GLFW_EXPOSE_NATIVE_NSGL
#elif BX_PLATFORM_WINDOWS
#	define GLFW_EXPOSE_NATIVE_WIN32
#	define GLFW_EXPOSE_NATIVE_WGL
#endif //
#include <GLFW/glfw3native.h>

#include <iostream>


static void *glfwNativeWindowHandle(GLFWwindow *_window) {
#	if BX_PLATFORM_LINUX
# 		if TINA_CONFIG_USE_WAYLAND
        wl_egl_window* win_impl = (wl_egl_window*)glfwGetWindowUserPointer(_window);
        if (!win_impl)
        {
            int width, height;
            glfwGetWindowSize(_window, &width, &height);
            struct wl_surface* surface = (struct wl_surface*)glfwGetWaylandWindow(_window);
            if (!surface)
                return nullptr;
            win_impl = wl_egl_window_create(surface, width, height);
            glfwSetWindowUserPointer(_window, (void*)(uintptr_t)win_impl);
        }
        return (void*)(uintptr_t)win_impl;
#		else
    return (void *) (uintptr_t) glfwGetX11Window(_window);
#		endif
#	elif BX_PLATFORM_OSX
        return glfwGetCocoaWindow(_window);
#	elif BX_PLATFORM_WINDOWS
        return glfwGetWin32Window(_window);
#	endif // BX_PLATFORM_
}

static void glfwDestroyWindowImpl(GLFWwindow *_window) {
    if (!_window)
        return;
#	if BX_PLATFORM_LINUX
#		if TINA_CONFIG_USE_WAYLAND
        wl_egl_window* win_impl = (wl_egl_window*)glfwGetWindowUserPointer(_window);
        if (win_impl)
        {
            glfwSetWindowUserPointer(_window, nullptr);
            wl_egl_window_destroy(win_impl);
        }
#		endif
#	endif
    glfwDestroyWindow(_window);
}


static void errorCb(int _error, const char *_description) {
    printf("GLFW error %d: %s", _error, _description);
}


void *getNativeDisplayHandle() {
#	if BX_PLATFORM_LINUX
#		if TINA_CONFIG_USE_WAYLAND
        return glfwGetWaylandDisplay();
#		else
    return glfwGetX11Display();
#		endif // ENTRY_CONFIG_USE_WAYLAND
#	else
    return NULL;
#	endif // BX_PLATFORM_*
}

bgfx::NativeWindowHandleType::Enum getNativeWindowHandleType() {
#	if BX_PLATFORM_LINUX
#		if TINA_CONFIG_USE_WAYLAND
        return bgfx::NativeWindowHandleType::Wayland;
#		else
    return bgfx::NativeWindowHandleType::Default;
#		endif // ENTRY_CONFIG_USE_WAYLAND
#	else
    return bgfx::NativeWindowHandleType::Default;
#	endif // BX_PLATFORM_*
}

int main(int argc, char *argv[]) {
#if BX_PLATFORM_LINUX
#if TINA_CONFIG_USE_WAYLAND
    if (glfwPlatformSupported(GLFW_PLATFORM_WAYLAND))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
#else
    if (glfwPlatformSupported(GLFW_PLATFORM_X11))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
#elif BX_PLATFORM_WINDOWS
    if (glfwPlatformSupported(GLFW_PLATFORM_WIN32))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WIN32);
#elif BX_PLATFORM_OSX
    if (glfwPlatformSupported(GLFW_PLATFORM_COCOA))
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_COCOA);
#endif

    glfwSetErrorCallback(errorCb);

    if (!glfwInit()) {
        return bx::kExitFailure;
    }

    int m_width = 1280;
    int m_height = 720;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow *m_window = glfwCreateWindow(m_width, m_height, "Tina", nullptr, nullptr);
    if (!m_window) return bx::kExitFailure;

    bgfx::Init init;
    init.resolution.width = m_width;
    init.resolution.height = m_height;
    init.resolution.reset = BGFX_RESET_VSYNC;
    init.resolution.format = bgfx::TextureFormat::RGBA8;

    init.platformData.ndt = getNativeDisplayHandle();
    init.platformData.nwh = glfwNativeWindowHandle(m_window);
    init.platformData.type = getNativeWindowHandleType();

    if (!bgfx::init(init)) {
        return bx::kExitFailure;
    }

    bgfx::FrameBufferHandle frameBufferHandle = bgfx::createFrameBuffer(glfwNativeWindowHandle(m_window), m_width,
                                                                        m_height);
    bgfx::setViewFrameBuffer(0, frameBufferHandle);
    bgfx::setViewRect(0, 0, 0, uint16_t(m_width), uint16_t(m_height));
    bgfx::setViewClear(0
                       , BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH
                       , 0x00ff00ff
                       , 1.0f
                       , 0
    );
    bgfx::touch(0);

    while (!glfwWindowShouldClose(m_window)) {
        bgfx::frame();
        glfwPollEvents();
    }
    bgfx::shutdown();
    glfwDestroyWindowImpl(m_window);
    glfwTerminate();
    return 0;
}
