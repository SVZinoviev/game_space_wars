idf_component_register(SRCS "main.c" "game_renderer.c" "lowpixel20_font.c"
                    INCLUDE_DIRS "."
                    REQUIRES gfx ili9341 ft6336u)
