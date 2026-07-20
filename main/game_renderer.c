#include "game_renderer.h"
#include <math.h>
#include <string.h>

static const gfx_color_t orange = gfx_rgb888_to_rgb565(210, 90, 22);
static const gfx_color_t black = gfx_rgb888_to_rgb565(0, 0, 0);

struct Vect2f {
    float x;
    float y;
};

#define ToRad(x) ((M_PI/180.0f)*(x))

static struct Vect2f rotate(struct Vect2f vect, float angle_deg)
{
    struct Vect2f vect_rotated;

    /*
    Rotation matrix
    | cos(Theta) -sin(Theta) |
    | sin(Theta) cos(Theta)  |
    */

    float T = ToRad(angle_deg);
    vect_rotated.x = vect.x * cosf(T) - vect.y * sinf(T);
    vect_rotated.y = vect.x * sinf(T) + vect.y * cosf(T);

    return vect_rotated; 
}

#define SHIP_PTS 4
static struct Vect2f ship[4] = {{0, 14}, {5, -6}, {0, -4}, {-5, -6}}; 
static struct Vect2f ship_pos = {160, 120};
static float ship_angle = 0;

void render_spaceship(struct gfx *gfx)
{
    static struct Vect2f rotated_ship[SHIP_PTS];

    // Rotate all points
    for (int n = 0; n < SHIP_PTS; n++) {
        rotated_ship[n] = rotate(ship[n], ship_angle);
    }

    for (int n = 0; n < SHIP_PTS - 1; n++) {
        gfx_line(gfx, rotated_ship[n].x + ship_pos.x, rotated_ship[n].y + ship_pos.y,
                 rotated_ship[n + 1].x + ship_pos.x, rotated_ship[n + 1].y + ship_pos.y,
                 1, orange);
    }
    gfx_line(gfx, rotated_ship[SHIP_PTS - 1].x + ship_pos.x, rotated_ship[SHIP_PTS - 1].y + ship_pos.y,
                  rotated_ship[0].x + ship_pos.x, rotated_ship[0].y + ship_pos.y,
                  1, orange);
}

struct Button {
    struct Vect2f rect[2];
    gfx_color_t color;
    const char *text;
};

#define BTN_NUM 3
static struct Button buttons[BTN_NUM] = {
    {{{3,  230}, {106, 180}}, orange, "CCW"},
    {{{110,230}, {211, 180}}, orange, "THR"},
    {{{215,230}, {318, 180}}, orange, "CW"},
};

void render_buttons(struct gfx *gfx)
{
    for (int n = 0; n < BTN_NUM; n++) {
        gfx_rect(gfx, buttons[n].rect[0].x, buttons[n].rect[0].y,
                      buttons[n].rect[1].x, buttons[n].rect[1].y,
                      1, buttons[n].color, false, black);
    }
}

void rotate_ship_cw()
{
    ship_angle += 3;
    if (ship_angle > 360.0f) {
        ship_angle -= 360;
    }
} 

void rotate_ship_ccw()
{
    ship_angle -= 3;
    // Wrap
    if (ship_angle < 0) {
        ship_angle += 360.0f;
    }
}

void thrust_increase()
{
    
}