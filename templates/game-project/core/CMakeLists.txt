# Portable content. Add every gameplay, scene, and UI source here.
#
# LINK takes engine modules (Tina::Physics2D, Tina::Audio, ...) but no platform target:
# tina_add_game_content rejects those at configure time, because a platform link would
# quietly make these sources desktop-only.
tina_add_game_content(mygame_content
    SOURCES
        GameApplication.hpp
        GameApplication.cpp
    LINK
        Tina::Asset
)
