#pragma once
#include "gfx.h"

void render_spaceship(struct gfx *gfx);
void render_buttons(struct gfx *gfx);
void render_planets(struct gfx *gfx);

void rotate_ship_cw();
void rotate_ship_ccw();
void thrust_increase();