#include "game_renderer.h"
#include <math.h>
#include <string.h>

#define ARRAY_ELEMENTS_COUNT(x) (sizeof((x)) / (sizeof((x)[0])))

static const gfx_color_t orange = gfx_rgb888_to_rgb565(210, 90, 22);
static const gfx_color_t red_flame = gfx_rgb888_to_rgb565(127, 10, 30);
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

static float distance(struct Vect2f a, struct Vect2f b)
{
    return sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y));
}

static struct Vect2f vect_sum(struct Vect2f a, struct Vect2f b)
{
    struct Vect2f sum;
    sum.x = a.x + b.x;
    sum.y = a.y + b.y;
    return sum;
}

static struct Vect2f vect_diff(struct Vect2f a, struct Vect2f b)
{
    struct Vect2f sum;
    sum.x = a.x - b.x;
    sum.y = a.y - b.y;
    return sum;
}

static float vect_len(struct Vect2f a)
{
    return sqrtf(a.x * a.x + a.y * a.y);
}

static void draw_contour(struct gfx *gfx, struct Vect2f *contour_points, size_t contour_points_num,
                         struct Vect2f offset, gfx_color_t color)
{
    for (int n = 0; n < contour_points_num - 1; n++) {
        gfx_line(gfx, contour_points[n].x + offset.x, contour_points[n].y + offset.y,
                 contour_points[n + 1].x + offset.x, contour_points[n + 1].y + offset.y,
                 1, color);
    }
    gfx_line(gfx, contour_points[contour_points_num - 1].x + offset.x, contour_points[contour_points_num - 1].y + offset.y,
                  contour_points[0].x + offset.x, contour_points[0].y + offset.y,
                  1, color);
}

#define SHIP_PTS 4
static struct Vect2f ship[SHIP_PTS] = {{0, 14}, {5, -6}, {0, -4}, {-5, -6}}; 
#define FLAME_PTS 4
#define FLAME_TIP_IDX 2
static struct Vect2f flame[FLAME_PTS] = {{0, -5}, {5, -8}, {0, -9}, {-5, -8}};
static struct Vect2f ship_pos = {160, 120};
static float ship_angle = 180;
static float ship_angle_speed = 0;
static const float ship_mass = 20;
static bool is_thrust = false;
static float thrust = 0;

static struct Vect2f velocity_vect = {0.0f, 0.0f};
static struct Vect2f position = {0.0f, 0.0f};

void calc_ship_dynamics()
{
    ship_angle += ship_angle_speed;
    thrust *= 0.9;

    if (ship_angle > 360.0f) {
        ship_angle -= 360;
    }
    // Wrap
    if (ship_angle < 0) {
        ship_angle += 360.0f;
    }

    struct Vect2f thrust_vect = {0, -thrust / ship_mass};
    thrust_vect = rotate(thrust_vect, ship_angle);

    // Integrate velocity
    velocity_vect.x += thrust_vect.x;
    velocity_vect.y += thrust_vect.y;

    // Integrate position
    position.x += velocity_vect.x;
    position.y += velocity_vect.y;
}

void render_spaceship(struct gfx *gfx)
{
    static struct Vect2f rotated_ship[SHIP_PTS];
    static struct Vect2f rotated_flame[FLAME_PTS];

    // Rotate all points of the ship and draw it
    for (int n = 0; n < SHIP_PTS; n++) {
        rotated_ship[n] = rotate(ship[n], ship_angle);
    }
    draw_contour(gfx, rotated_ship, SHIP_PTS, ship_pos, orange);

    if (thrust > 0.01) {
        for (int n = 0; n < FLAME_PTS; n++) {
            struct Vect2f pt = flame[n];
            if (n == FLAME_TIP_IDX) {
                pt.y += (-16 - (rand() & 0x3)) * thrust; // Randomize flame length
            }
            rotated_flame[n] = rotate(pt, ship_angle);
        }
        draw_contour(gfx, rotated_flame, FLAME_PTS, ship_pos, red_flame);
        is_thrust = false;
    }
}

struct Planet {
    struct Vect2f pos;
    unsigned int diameter;
    float mass;
};

static struct Planet planets[] = {{.pos = {.x = 50.0f, .y = 88.0f}, .diameter = 24, .mass = 100000.0f}};

void render_planets(struct gfx *gfx)
{
    for (int n = 0; n < ARRAY_ELEMENTS_COUNT(planets); n++) {
        gfx_circle(gfx, planets[n].pos.x + position.x, planets[n].pos.y + position.y,
                        planets[n].diameter / 2, 2, orange, true, orange);
    }
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
    if (ship_angle_speed < 20.0) {
        ship_angle_speed += 0.2f;
    }
} 

void rotate_ship_ccw()
{
    if (ship_angle_speed > -20.0f) {
        ship_angle_speed -= 0.2f;
    }
}

void thrust_increase()
{
    is_thrust = true;
    // Increase thrust
    if (thrust < 1.0f) {
        thrust += 0.05f;
    }
}