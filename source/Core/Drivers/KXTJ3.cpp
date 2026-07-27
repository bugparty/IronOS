/*
 * KXTJ3.cpp
 *
 *  Created on: 14 May 2026
 *      Author: OK2CM
 */

#include "KXTJ3_defines.h"
#include "accelerometers_common.h"
#include <KXTJ3.hpp>

static int16_t ax, ay, az;

bool KXTJ3::detect() { return (ACCEL_I2C_CLASS::probe(KXTJ3_I2C_ADDRESS) && (ACCEL_I2C_CLASS::I2C_RegisterRead(KXTJ3_I2C_ADDRESS, KXTJ3_REG_WHO_AM_I) == KXTJ3_REG_WHO_AM_I_ID)); }

static const ACCEL_I2C_CLASS::I2C_REG i2c_registers[] = {
    {    KXTJ3_REG_CTRL_REG1, 0x00, 0}, // disable mode
    {KXTJ3_REG_DATA_CTRL_REG, 0x02, 0}, // 50 Hz output data rate
    {    KXTJ3_REG_CTRL_REG1, 0xC0, 0}, // enabled mode, +/-2G range
};

bool KXTJ3::initalize() { return ACCEL_I2C_CLASS::writeRegistersBulk(KXTJ3_I2C_ADDRESS, i2c_registers, sizeof(i2c_registers) / sizeof(i2c_registers[0])); }

Orientation KXTJ3::getOrientation() {
  if (ax > KXJT3_ORIENTATION_THRESHOLD)
    return Orientation::ORIENTATION_RIGHT_HAND;
  else if (ax < -KXJT3_ORIENTATION_THRESHOLD)
    return Orientation::ORIENTATION_LEFT_HAND;
  else
    return Orientation::ORIENTATION_FLAT;
}

void KXTJ3::getAxisReadings(int16_t &x, int16_t &y, int16_t &z) {
  uint8_t temp[6];
  // Bulk read all 6 regs
  ACCEL_I2C_CLASS::Mem_Read(KXTJ3_I2C_ADDRESS, KXTJ3_REG_XOUT_L, temp, 6);
  x  = int16_t(((int16_t)temp[1]) << 8 | temp[0]) >> 2;
  y  = int16_t(((int16_t)temp[3]) << 8 | temp[2]) >> 2;
  z  = int16_t(((int16_t)temp[5]) << 8 | temp[4]) >> 2;
  ax = x;
  ay = y;
  az = z;
}
