SET(SOURCES screens/ui_Screen1.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_monster_png.c
    images/ui_img_shoe_32x32_png.c
    images/ui_img_1418925964.c
    images/ui_img_1_png.c
    images/ui_img_timer_png.c)

add_library(ui ${SOURCES})
