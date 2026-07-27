/*
 * KXTJ3_defines.h
 *
 *  Created on: 14 May 2026
 *      Author: OK2CM
 */

#ifndef DRIVERS_KXTJ3_DEFINES_H_
#define DRIVERS_KXTJ3_DEFINES_H_

#define KXJT3_ORIENTATION_THRESHOLD 2000

#define KXTJ3_I2C_ADDRESS     (0x0E << 1)
#define KXTJ3_REG_WHO_AM_I_ID 0x35

#define KXTJ3_REG_XOUT_L        0x06
#define KXTJ3_REG_XOUT_H        0x07
#define KXTJ3_REG_YOUT_L        0x08
#define KXTJ3_REG_YOUT_H        0x09
#define KXTJ3_REG_ZOUT_L        0x0A
#define KXTJ3_REG_ZOUT_H        0x0B
#define KXTJ3_REG_WHO_AM_I      0x0F
#define KXTJ3_REG_DATA_CTRL_REG 0x21
#define KXTJ3_REG_CTRL_REG1     0x1B

#endif /* DRIVERS_KXTJ3_DEFINES_H_ */
