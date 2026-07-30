#pragma once
#include "gfx.h"

void calc_ship_dynamics();


void render_spaceship(struct gfx *gfx);
void render_buttons(struct gfx *gfx);
void generate_planets();
void render_planets(struct gfx *gfx);
void render_parameters(struct gfx *gfx);

void rotate_ship_cw();
void rotate_ship_ccw();
void thrust_increase();