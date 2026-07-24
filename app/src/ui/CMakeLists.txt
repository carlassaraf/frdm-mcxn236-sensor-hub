SET(SOURCES screens/ui_scrSplash.c
    screens/ui_scrOverview.c
    screens/ui_scrTilt.c
    screens/ui_scrEnvironment.c
    screens/ui_scrCan.c
    ui_theme_manager.c
    ui_themes.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_651563667.c
    images/ui_img_1390009267.c
    images/ui_img_563396740.c
    images/ui_img_1090598983.c
    images/ui_img_18748633.c
    images/ui_img_1641652374.c
    fonts/ui_font_InterBold28.c
    fonts/ui_font_InterMedium11.c
    fonts/ui_font_InterRegular13.c
    fonts/ui_font_InterSemiBold16.c
    fonts/ui_font_JetBrainsMedium14.c)

add_library(ui ${SOURCES})
