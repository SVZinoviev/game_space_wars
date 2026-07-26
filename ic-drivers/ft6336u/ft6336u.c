/*
 * ft6336u.c - FocalTech FT6336U capacitive touch panel controller driver
 *
 * Platform-independent driver core. All OS- and hardware-specific services
 * (I2C transfers, locking) are provided by the user through
 * struct ft6336_interface.
 *
 * Copyright (C) 2026 Sergio <svzinoviev@gmail.com>
 */

#include <ft6336u.h>
#include <stdint.h>

//----------Register list-------------
enum ft6336_registers {
    FT6336_REG_DEV_MODE   = 0x00,  // Device mode (0 = normal working mode)
    FT6336_REG_TD_STATUS  = 0x02,  // Number of touch points in low nibble
    FT6336_REG_P1_XH      = 0x03,  // Point 1: event flag + X[11:8]
    FT6336_REG_P1_XL      = 0x04,  // Point 1: X[7:0]
    FT6336_REG_P1_YH      = 0x05,  // Point 1: Y[11:8]
    FT6336_REG_P1_YL      = 0x06,  // Point 1: Y[7:0]
    FT6336_REG_THRESHOLD  = 0x80,  // Touch detect threshold (ID_G_THGROUP)
    FT6336_REG_FILTER_COE = 0x85,  // Filter coefficient / noise filter (ID_G_FILTER_COE)
    FT6336_REG_RATE_ACTIVE = 0x88, // Report rate in active mode (ID_G_PERIODACTIVE)
    FT6336_REG_CHIP_ID    = 0xA3,  // Chip selecting register (FT6336U = 0x64)
};

/* Default report period written to FT6336_REG_RATE_ACTIVE during init. */
#define FT6336_RATE_ACTIVE_DEFAULT 0x0E

/* Touch data block read in one burst: TD_STATUS .. P1_YL. */
#define FT6336_TOUCH_BLOCK_FIRST FT6336_REG_TD_STATUS
#define FT6336_TOUCH_BLOCK_LEN   5

//----------Private functions-------------
// Locking is delegated to the interface. NULL lock/unlock means no locking.
static int lock(struct ft6336_instance *pinstance)
{
    if (pinstance->iface.lock == NULL) {
        return 0;
    }
    return pinstance->iface.lock();
}

static int unlock(struct ft6336_instance *pinstance)
{
    if (pinstance->iface.unlock == NULL) {
        return 0;
    }
    return pinstance->iface.unlock();
}

// Applies the optional swap/invert mapping to a raw controller point.
static void map_xy(struct ft6336_instance *pinstance, uint16_t *px, uint16_t *py)
{
    uint16_t x = *px;
    uint16_t y = *py;

    if (pinstance->swap_xy) {
        uint16_t t = x;
        x = y;
        y = t;
    }
    if (pinstance->invert_x && pinstance->width) {
        x = (pinstance->width - 1) - x;
    }
    if (pinstance->invert_y && pinstance->height) {
        y = (pinstance->height - 1) - y;
    }
    *px = x;
    *py = y;
}

static bool point_in_zone(const struct ft6336_zone *pzone, uint16_t x, uint16_t y)
{
    uint16_t xa = pzone->x0, xb = pzone->x1, ya = pzone->y0, yb = pzone->y1;
    if (xa > xb) { uint16_t t = xa; xa = xb; xb = t; }
    if (ya > yb) { uint16_t t = ya; ya = yb; yb = t; }
    return (x >= xa && x <= xb && y >= ya && y <= yb);
}
//------------END OF PRIVATE--------------------

int ft6336_init(struct ft6336_instance *pinstance, struct ft6336_interface *piface,
                uint8_t threshold, uint8_t filter)
{
    int retval = FT6336_ERROR;
    uint8_t chip_id = 0;
    uint8_t mode = 0x00;
    uint8_t rate = FT6336_RATE_ACTIVE_DEFAULT;
    uint8_t filt = filter;

    if (pinstance == NULL || piface == NULL ||
        piface->i2c_read == NULL || piface->i2c_write == NULL) {
        goto exit;
    }

    pinstance->iface.i2c_read  = piface->i2c_read;
    pinstance->iface.i2c_write = piface->i2c_write;
    pinstance->iface.lock      = piface->lock;
    pinstance->iface.unlock    = piface->unlock;
    if (pinstance->address == 0) {
        pinstance->address = FT6336_BUS_ADDRESS;
    }

    if (lock(pinstance)) {
        goto exit;
    }

    if (pinstance->iface.i2c_read(pinstance->address, FT6336_REG_CHIP_ID,
                                  &chip_id, 1) != 0) {
        goto exit_unlock;
    }
    if (chip_id != FT6336_CHIP_ID) {
        goto exit_unlock;
    }
    // Normal working mode.
    if (pinstance->iface.i2c_write(pinstance->address, FT6336_REG_DEV_MODE,
                                   &mode, 1) != 0) {
        goto exit_unlock;
    }
    // Touch detection threshold.
    if (pinstance->iface.i2c_write(pinstance->address, FT6336_REG_THRESHOLD,
                                   &threshold, 1) != 0) {
        goto exit_unlock;
    }
    // Filter coefficient (noise filtering).
    if (pinstance->iface.i2c_write(pinstance->address, FT6336_REG_FILTER_COE,
                                   &filt, 1) != 0) {
        goto exit_unlock;
    }
    // Report rate in active mode.
    if (pinstance->iface.i2c_write(pinstance->address, FT6336_REG_RATE_ACTIVE,
                                   &rate, 1) != 0) {
        goto exit_unlock;
    }

    retval = FT6336_OK;

exit_unlock:
    if (unlock(pinstance)) {
        retval = FT6336_ERROR;
    }
    if (retval == FT6336_OK) {
        // Clear zone registry and edge state only after a clean bring-up.
        for (int i = 0; i < FT6336_MAX_ZONES; i++) {
            pinstance->zones[i].used = false;
        }
        pinstance->was_touched = false;
    }
exit:
    return retval;
}

int ft6336_set_filter(struct ft6336_instance *pinstance, uint8_t coefficient)
{
    int retval = FT6336_ERROR;

    if (pinstance == NULL) {
        goto exit;
    }
    if (lock(pinstance)) {
        goto exit;
    }
    if (pinstance->iface.i2c_write(pinstance->address, FT6336_REG_FILTER_COE,
                                   &coefficient, 1) == 0) {
        retval = FT6336_OK;
    }
    if (unlock(pinstance)) {
        retval = FT6336_ERROR;
    }

exit:
    return retval;
}

int ft6336_get_filter(struct ft6336_instance *pinstance, uint8_t *pcoefficient)
{
    int retval = FT6336_ERROR;

    if (pinstance == NULL || pcoefficient == NULL) {
        goto exit;
    }
    if (lock(pinstance)) {
        goto exit;
    }
    if (pinstance->iface.i2c_read(pinstance->address, FT6336_REG_FILTER_COE,
                                  pcoefficient, 1) == 0) {
        retval = FT6336_OK;
    }
    if (unlock(pinstance)) {
        retval = FT6336_ERROR;
    }

exit:
    return retval;
}

int ft6336_get_coordinates(struct ft6336_instance *pinstance, bool *ptouch,
                           uint16_t *px, uint16_t *py)
{
    int retval = FT6336_ERROR;
    uint8_t buf[FT6336_TOUCH_BLOCK_LEN];

    if (pinstance == NULL || ptouch == NULL || px == NULL || py == NULL) {
        goto exit;
    }

    if (lock(pinstance)) {
        goto exit;
    }
    if (pinstance->iface.i2c_read(pinstance->address, FT6336_TOUCH_BLOCK_FIRST,
                                  buf, sizeof(buf)) != 0) {
        goto exit_unlock;
    }
    retval = FT6336_OK;

exit_unlock:
    if (unlock(pinstance)) {
        retval = FT6336_ERROR;
    }
    if (retval != FT6336_OK) {
        goto exit;
    }

    // buf[0] = TD_STATUS, buf[1] = P1_XH, buf[2] = P1_XL,
    // buf[3] = P1_YH,    buf[4] = P1_YL.
    if ((buf[0] & 0x0F) == 0) {
        *ptouch = false;
        *px = 0;
        *py = 0;
    } else {
        uint16_t x = (uint16_t)((buf[1] & 0x0F) << 8) | buf[2];
        uint16_t y = (uint16_t)((buf[3] & 0x0F) << 8) | buf[4];
        map_xy(pinstance, &x, &y);
        *ptouch = true;
        *px = x;
        *py = y;
    }

exit:
    return retval;
}

int ft6336_register_zone(struct ft6336_instance *pinstance, uint16_t x0,
                         uint16_t y0, uint16_t x1, uint16_t y1,
                         ft6336_zone_cb_t callback, void *puser)
{
    int retval = FT6336_ERROR;

    if (pinstance == NULL || callback == NULL) {
        goto exit;
    }
    for (int i = 0; i < FT6336_MAX_ZONES; i++) {
        if (!pinstance->zones[i].used) {
            pinstance->zones[i].x0 = x0;
            pinstance->zones[i].y0 = y0;
            pinstance->zones[i].x1 = x1;
            pinstance->zones[i].y1 = y1;
            pinstance->zones[i].callback = callback;
            pinstance->zones[i].puser = puser;
            pinstance->zones[i].used = true;
            retval = i;
            break;
        }
    }

exit:
    return retval;
}

int ft6336_unregister_zone(struct ft6336_instance *pinstance, int zone_id)
{
    int retval = FT6336_ERROR;

    if (pinstance == NULL || zone_id < 0 || zone_id >= FT6336_MAX_ZONES) {
        goto exit;
    }
    pinstance->zones[zone_id].used = false;
    retval = FT6336_OK;

exit:
    return retval;
}

int ft6336_poll(struct ft6336_instance *pinstance)
{
    int retval;
    bool touched = false;
    uint16_t x = 0, y = 0;

    if (pinstance == NULL) {
        return FT6336_ERROR;
    }

    retval = ft6336_get_coordinates(pinstance, &touched, &x, &y);
    if (retval != FT6336_OK) {
        return retval;
    }

    // Dispatch only on the press-down edge so each touch fires a zone once.
    if (touched && !pinstance->was_touched) {
        for (int i = 0; i < FT6336_MAX_ZONES; i++) {
            if (pinstance->zones[i].used &&
                point_in_zone(&pinstance->zones[i], x, y)) {
                pinstance->zones[i].callback(pinstance, x, y,
                                             pinstance->zones[i].puser);
            }
        }
    }
    //pinstance->was_touched = touched;

    return FT6336_OK;
}
