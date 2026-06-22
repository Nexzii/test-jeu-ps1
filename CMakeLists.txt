cmake_minimum_required(VERSION 3.21)

project(
    ff_psx
    LANGUAGES    C ASM
    VERSION      0.1.0
    DESCRIPTION  "RPG style FF pour PS1"
)

psn00bsdk_add_executable(ff_psx GPREL
    src/main.c
    src/engine3d.c
    src/player.c
    src/battle.c
    src/map.c
    src/text.c
    src/menu.c
    src/dialogue.c
    src/shop.c
    src/model_player.c
    src/model_tree.c
    src/model_cloud.c
    src/audio.c
    src/textures.c
)

# Textures (16-bit BGR555 raw) embedded directly into the executable.
psn00bsdk_target_incbin(ff_psx PRIVATE tex_stone  ${PROJECT_SOURCE_DIR}/assets/tex/stone.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_brick  ${PROJECT_SOURCE_DIR}/assets/tex/brick.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_wood   ${PROJECT_SOURCE_DIR}/assets/tex/wood.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_roof   ${PROJECT_SOURCE_DIR}/assets/tex/roof.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_grass  ${PROJECT_SOURCE_DIR}/assets/tex/grass.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_tree   ${PROJECT_SOURCE_DIR}/assets/tex/tree.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_cloud  ${PROJECT_SOURCE_DIR}/assets/tex/cloud.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_dialog ${PROJECT_SOURCE_DIR}/assets/tex/dialog.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE tex_bg     ${PROJECT_SOURCE_DIR}/assets/tex/bg_battle.bin)
psn00bsdk_target_incbin(ff_psx PRIVATE wind_adpcm ${PROJECT_SOURCE_DIR}/assets/snd/wind.bin)

configure_file(${PROJECT_SOURCE_DIR}/cd/system.cnf ${CMAKE_BINARY_DIR}/system.cnf COPYONLY)
psn00bsdk_add_cd_image(iso ff_psx cd/iso.xml DEPENDS ff_psx)
