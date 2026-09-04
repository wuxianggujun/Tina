# The worked example of the content/frontend split, and the reason the split is enforced: the
# Android JNI bridge links this content library directly, so the scenes reach a device without
# being rewritten.
#
# tina_add_game_content refuses a platform link outright, which is what keeps that true as scenes
# are added.
tina_add_game_content(tina_sample_gallery
    SOURCES
        GalleryActions.hpp
        GalleryActions.cpp
        GalleryScene.hpp
        GalleryMenu.cpp
        GalleryScenes.cpp
)
add_library(Tina::SampleGallery ALIAS tina_sample_gallery)
