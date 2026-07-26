/*
 * ft6336u.h - FocalTech FT6336U capacitive touch panel controller driver
 *
 * Platform-independent: fill struct ft6336_interface with the I2C
 * read/write and (optionally) lock/unlock callbacks for your platform,
 * then pass it to ft6336_init().
 *
 * Copyright (C) 2026 Sergio <svzinoviev@gmail.com>
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum number of touch zones registrable per instance. */
#ifndef FT6336_MAX_ZONES
#define FT6336_MAX_ZONES 8
#endif

enum ft6336_addresses {
    FT6336_BUS_ADDRESS = 0x38,  // Fixed 7-bit I2C address of the FT6336U
};

enum ft6336_ret_codes { FT6336_OK = 0, FT6336_ERROR };

/* Expected value of the CHIP_ID register (0xA3) for the FT6336U. */
enum ft6336_ids { FT6336_CHIP_ID = 0x64 };

struct ft6336_instance;  // forward declaration for the callback type

/**
 * @brief Touch-zone callback type.
 *
 * Invoked by ft6336_poll() when a new touch (press-down edge) falls inside a
 * registered zone. Coordinates are in the same (optionally mapped) space as
 * ft6336_get_coordinates() reports.
 *
 * @param pinstance The instance that detected the touch.
 * @param x Touch X coordinate.
 * @param y Touch Y coordinate.
 * @param puser The opaque pointer supplied at ft6336_register_zone().
 */
typedef void (*ft6336_zone_cb_t)(struct ft6336_instance *pinstance,
                                 uint16_t x, uint16_t y, void *puser);

// Interface structure. Must be filled with function pointers for I2C register
// access. lock/unlock are optional and may be NULL if thread safety is not
// required.
struct ft6336_interface {
    // Reads len bytes starting at reg over I2C. Returns 0 on success.
    int (*i2c_read)(uint8_t dev_addr, uint8_t reg, uint8_t *pdata, size_t len);
    // Writes len bytes starting at reg over I2C. Returns 0 on success.
    int (*i2c_write)(uint8_t dev_addr, uint8_t reg, const uint8_t *pdata, size_t len);
    int (*lock)(void);    // Takes the driver mutex, returns 0 on success. May be NULL.
    int (*unlock)(void);  // Gives the driver mutex, returns 0 on success. May be NULL.
};

// A registered rectangular touch zone (private; managed via the API below).
struct ft6336_zone {
    uint16_t x0, y0, x1, y1;     // Inclusive rectangle bounds
    ft6336_zone_cb_t callback;   // Fired on press-down inside the rectangle
    void *puser;                 // Opaque context passed to the callback
    bool used;                   // Slot occupied flag
};

// Instance structure.
struct ft6336_instance {
    struct ft6336_interface iface;
    uint8_t address;  // HW I2C address, normally FT6336_BUS_ADDRESS

    // Optional mapping of raw controller coordinates to display space. Leave
    // zeroed/false for a 1:1 passthrough. invert_x/invert_y require the
    // matching width/height to be set.
    uint16_t width;   // Display width in pixels (for invert_x), 0 = disabled
    uint16_t height;  // Display height in pixels (for invert_y), 0 = disabled
    bool swap_xy;     // Exchange X and Y (applied before inversion)
    bool invert_x;    // Mirror X about width
    bool invert_y;    // Mirror Y about height

    // Private state.
    struct ft6336_zone zones[FT6336_MAX_ZONES];
    bool was_touched;  // Previous poll touch state, for press-down edge detection
};

/**
 * @brief Initializes the FT6336U: validates the interface, verifies the chip
 *        ID and puts the controller into normal operating mode.
 *
 * Clears any previously registered zones and resets edge-detection state.
 *
 * @param pinstance Pointer to the caller-allocated instance. Its @c address
 *        field is used (defaults to ::FT6336_BUS_ADDRESS if left 0).
 * @param piface Pointer to a properly filled interface structure.
 * @param threshold Touch-detection threshold written to the THGROUP register;
 *        higher values require a firmer touch (typical range ~20..60).
 * @param filter Filter coefficient written to the FILTER_COE register; higher
 *        values smooth coordinate noise at the cost of a little lag (0 = off).
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments, I2C failure or
 *         a chip-ID mismatch.
 */
int ft6336_init(struct ft6336_instance *pinstance, struct ft6336_interface *piface,
                uint8_t threshold, uint8_t filter);

/**
 * @brief Sets the FT6336U noise filter coefficient at runtime.
 *
 * @param pinstance Pointer to an initialized instance.
 * @param coefficient Filter coefficient (FILTER_COE); higher = more smoothing.
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments or I2C failure.
 */
int ft6336_set_filter(struct ft6336_instance *pinstance, uint8_t coefficient);

/**
 * @brief Reads the current FT6336U filter coefficient.
 *
 * @param pinstance Pointer to an initialized instance.
 * @param pcoefficient Destination for the FILTER_COE value (must not be NULL).
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments or I2C failure.
 */
int ft6336_get_filter(struct ft6336_instance *pinstance, uint8_t *pcoefficient);

/**
 * @brief Reads the current primary touch point.
 *
 * @param pinstance Pointer to an initialized instance.
 * @param ptouch Destination set to true if a point is currently touched,
 *        false otherwise (must not be NULL).
 * @param px Destination for the X coordinate; set to 0 when not touched
 *        (must not be NULL).
 * @param py Destination for the Y coordinate; set to 0 when not touched
 *        (must not be NULL).
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments or I2C failure.
 */
int ft6336_get_coordinates(struct ft6336_instance *pinstance, bool *ptouch,
                           uint16_t *px, uint16_t *py);

/**
 * @brief Registers a rectangular touch zone and its callback.
 *
 * The corners may be given in any order. The callback fires from
 * ft6336_poll() once per press that lands inside the rectangle.
 *
 * @param pinstance Pointer to an initialized instance.
 * @param x0 First corner X.
 * @param y0 First corner Y.
 * @param x1 Opposite corner X.
 * @param y1 Opposite corner Y.
 * @param callback Function to invoke on a press inside the zone (not NULL).
 * @param puser Opaque pointer passed back to the callback (may be NULL).
 *
 * @return The zone id (>= 0) on success, or FT6336_ERROR if arguments are bad
 *         or no free zone slot remains.
 */
int ft6336_register_zone(struct ft6336_instance *pinstance, uint16_t x0,
                         uint16_t y0, uint16_t x1, uint16_t y1,
                         ft6336_zone_cb_t callback, void *puser);

/**
 * @brief Removes a previously registered zone.
 *
 * @param pinstance Pointer to an initialized instance.
 * @param zone_id Id returned by ft6336_register_zone().
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments.
 */
int ft6336_unregister_zone(struct ft6336_instance *pinstance, int zone_id);

/**
 * @brief Samples the panel and dispatches zone callbacks.
 *
 * Reads the current touch point and, on a press-down edge (a touch that was
 * not present at the previous poll), invokes the callback of every registered
 * zone whose rectangle contains the point. Call this periodically, or from the
 * handler of the controller's INT line.
 *
 * @param pinstance Pointer to an initialized instance.
 *
 * @return FT6336_OK on success, FT6336_ERROR on bad arguments or I2C failure.
 */
int ft6336_poll(struct ft6336_instance *pinstance);

#ifdef __cplusplus
}
#endif
