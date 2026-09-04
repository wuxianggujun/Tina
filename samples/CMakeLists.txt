# gallery/ is deliberately NOT added here. The root CMakeLists adds it outside TINA_BUILD_EXAMPLES,
# because the Android APK links its scene library while building with examples off -- and adding it in
# both places would define the same targets twice.
#
# Every sample below follows the template layout: portable content in core/, the composition root in
# platforms/<frontend>/, source assets in assets/. Each sample's own CMakeLists.txt adds those
# subdirectories, so this file only decides which samples the current feature set can build.
#
# A sample with no core/ is not an exception to the split -- it is a claim that the program is
# nothing but a composition root, which is what tina_verify_game_content_portable() would have no
# content library to check.

add_subdirectory(null)
add_subdirectory(asset)
# Tina::Network is built unconditionally, so its first consumer outside the tests
# is too. Headless: no GPU, no window, no EngineHost.
add_subdirectory(network)
add_subdirectory(2d)
add_subdirectory(2d_tilemap)
add_subdirectory(3d)

if (TINA_BUILD_PHYSICS2D)
    add_subdirectory(physics2d_bench)
    # Tina::Gameplay2D only exists with Physics2D, and this sample is its first
    # consumer outside the tests. Headless, so it needs no GPU or window.
    add_subdirectory(2d_authored_scene)
endif ()

if(TINA_BUILD_PLATFORM_GLFW)
    add_subdirectory(platform)
endif()

if(EMSCRIPTEN)
    # Drives EngineHost::tick from emscripten_set_main_loop, because a browser owns the
    # frame loop and run() would have to block. Gated on the toolchain rather than an
    # option, for the same reason as the html5 platform backend itself.
    add_subdirectory(web)
endif()

if(TINA_BUILD_PLATFORM_GLFW AND TINA_BUILD_RENDER_BGFX)
    add_subdirectory(desktop)
    add_subdirectory(ui_showcase)
    add_subdirectory(desktop_shell)
    # On-screen thumbstick driven by pointer drag or WASD. Needs a real window and
    # UI backend, so it lives with the other desktop-only samples.
    add_subdirectory(virtual_stick)
    add_subdirectory(3d_bgfx)
    add_subdirectory(3d_product)
    add_subdirectory(2d_bgfx)
    # First consumer of the custom Sprite2D fragment shader path. Cooks its own shader payload
    # with tina_assetc at build time, so it needs both that tool and a host shaderc.
    add_subdirectory(2d_custom_shader)
    # One cooked program drawn with three independent uniform value sets, sampling a real uploaded
    # texture. Covers what 2d_custom_shader left unproven: per-material uniform isolation, batch
    # splitting on the uniform key, and the sampler path.
    add_subdirectory(2d_shader_materials)
    # Custom fragment that consumes the engine lighting/normal contract (point lights, shadow
    # segments, s_normalTex) rather than replacing it. Cooks its own payload at build time.
    add_subdirectory(2d_shader_lighting)
    # Two-bone IK driving a skinned mesh. The first sample to link Tina::Animation3D, which
    # until now was reachable only from tests: it pairs the solver's numeric result with a
    # frame capture, because a correct pose can still be lost in the palette upload, the
    # extraction copy or the skinned vertex program.
    add_subdirectory(3d_ik_chain)
    # The other half of Tina::Animation3D: ClipSampler3D, BlendTree3D and AnimationGraph3D, which
    # 3d_ik_chain leaves untouched because it links the module for solveTwoBoneIk alone. Desktop
    # rather than headless because the pixel half of its evidence needs a real device to capture.
    add_subdirectory(3d_animation_graph)
    add_subdirectory(2d_catalog)
    add_subdirectory(2d_tilemap_bgfx)
    # Procedurally generated destructible tile world; interactive, not a gate program.
    add_subdirectory(2d_terraria)
    # Reports the GPU's hardware video decode capabilities. Needs the production bgfx
    # device to answer at all, which is why it lives here rather than with the
    # headless samples.
    add_subdirectory(video_probe)
    # Demuxes an MP4 into the Annex B parameter sets and access units the decode SPI
    # expects, and checks them against that SPI's documented preconditions. It needs no
    # GPU, but l-smash only exists when bgfx is built, which is why it is guarded here.
    add_subdirectory(video_demux)
    # Closes the gap video_demux and video_probe leave: submits real access units to a
    # real device and proves the decoded picture reached the screen by comparing two
    # captures of the same scene, one with zero submitted AUs and one with many. A
    # falsifiable criterion that needs no fabricated threshold for what constitutes a
    # "real" picture.
    add_subdirectory(video_playback)
    # First-person voxel world: the Minecraft core loop. Interactive rather than a gate
    # program, and desktop-only because the cursor lock and mouse-look have no meaning
    # without a real window.
    add_subdirectory(3d_voxel)
endif()
