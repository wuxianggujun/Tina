# The desktop front-end. Only built when GLFW is available, like every other windowed sample.
tina_add_desktop_frontend(tina_sample_gallery_desktop
    SOURCES DesktopMain.cpp
    CONTENT Tina::SampleGallery
)
