#pragma once
#include <stdint.h>
/**
 * Configuration.h
 * Deep work in progress for Fnirsi HS-02A and HS-02B
 *
 */

#if defined(MODEL_HS02) == 0
#error "No model defined!"
#endif

//===========================================================================
//============================= Default Settings ============================
//===========================================================================
/**
 * Default soldering temp is 320.0 C
 * Temperature the iron sleeps at - default 150.0 C
 */

#define SLEEP_TEMP         150 // Default sleep temperature
#define BOOST_TEMP         420 // Default boost temp.
#define BOOST_MODE_ENABLED 1   // 0: Disable 1: Enable

/**
 * Blink the temperature on the cooling screen when its > 50C
 */
#define COOLING_TEMP_BLINK 0 // 0: Disable 1: Enable

/**
 * How many seconds/minutes we wait until going to sleep/shutdown.
 * Values -> SLEEP_TIME * 10; i.e. 5*10 = 50 Seconds!
 */
#define SLEEP_TIME    5  // x10 Seconds
#define SHUTDOWN_TIME 10 // Minutes

/**
 * Auto start off for safety.
 * Pissible values are:
 *  0 - none
 *  1 - Soldering Temperature
 *  2 - Sleep Temperature
 *  3 - Sleep Off Temperature
 */
#define AUTO_START_MODE 0 // Default to none

/**
 * Locking Mode
 * When in soldering mode a long press on both keys toggle the lock of the buttons
 * Possible values are:
 *  0 - Desactivated
 *  1 - Lock except boost
 *  2 - Full lock
 */
#define LOCKING_MODE 0 // Default to desactivated for safety

/**
 * OLED Orientation
 *
 */
#define ORIENTATION_MODE           2 // 0: Right 1:Left 2:Automatic - Default Automatic
#define MAX_ORIENTATION_MODE       2 // Up to auto
#define REVERSE_BUTTON_TEMP_CHANGE 0 // 0:Default 1:Reverse - Reverse the plus and minus button assigment for temperature change

/**
 * LCD Brightness
 * Range 0 - 127; however, thee is no noticable increase over 100.
 */
#define MIN_BRIGHTNESS     1    // Min LCD brightness selectable
#define MAX_BRIGHTNESS     105  // Max LCD brightness selectable

#define BRIGHTNESS_STEP    13   // LCD brightness increment
#define DEFAULT_BRIGHTNESS 105  // default LCD brightness

/**
 * Buzzer (annoying)
 *
 */
#define BUZZER_VOLUME 0 // Range: 0 - 20

/**
 * Button backlight <- ->
 * 
 */
#define BUTTON_BACKLIGHT 1 // 0: Disabled, 1: Enabled at boot

/**
 * Temp change settings
 */
#define TEMP_CHANGE_SHORT_STEP     1  // Default temp change short step +1
#define TEMP_CHANGE_LONG_STEP      10 // Default temp change long step +10
#define TEMP_CHANGE_SHORT_STEP_MAX 50 // Temp change short step MAX value
#define TEMP_CHANGE_LONG_STEP_MAX  90 // Temp change long step MAX value

/* Power pulse for keeping power banks awake*/
#define POWER_PULSE_INCREMENT    1
#define POWER_PULSE_MAX          100 // x10 max watts
#define POWER_PULSE_WAIT_MAX     9   // 9*2.5s = 22.5 seconds
#define POWER_PULSE_DURATION_MAX 9   // 9*250ms = 2.25 seconds

#define POWER_PULSE_DEFAULT 5
#define POWER_PULSE_WAIT_DEFAULT     4 // Default rate of the power pulse: 4*2500 = 10000 ms = 10 s
#define POWER_PULSE_DURATION_DEFAULT 1 // Default duration of the power pulse: 1*250 = 250 ms

/**
 * OLED Orientation Sensitivity on Automatic mode!
 * Motion Sensitivity <0=Off 1=Least Sensitive 9=Most Sensitive>
 */
#define SENSITIVITY 7 // Default 7

/**
 * Detailed soldering screen
 * Detailed idle screen (off for first time users)
 */
#define DETAILED_SOLDERING 1 // 0: Disable 1: Enable - Default 0
#define DETAILED_IDLE      1 // 0: Disable 1: Enable - Default 0

#define THERMAL_RUNAWAY_TIME_SEC 5
#define THERMAL_RUNAWAY_TEMP_C   3

#define CUT_OUT_SETTING          0  // default to no cut-off voltage
#define RECOM_VOL_CELL           33 // Minimum voltage per cell (Recommended 3.3V (33))
#define TEMPERATURE_INF          0  // default to 0
#define DESCRIPTION_SCROLL_SPEED 0  // 0: Slow 1: Fast - default to slow
#define ANIMATION_LOOP           1  // 0: off 1: on
#define ANIMATION_SPEED          settingOffSpeed_t::MEDIUM

#define ADC_MAX_READING  (4096 * 8)  // oversample 12bit adc 8 times
#define ADC_VDD_MV        3300       // ADC max reading millivolts

// #define POW_PD 0
#define POW_PD_EXT 3 // TODO: implement
#define POW_DC

// Deriving the Voltage div:
// Vin_max = (3.3*(r1+r2))/(r2)
// vdiv = (32768*4)/(vin_max*10)

// #if defined(MODEL_HS02A) + defined(MODEL_HS02B) > 1
// #error "Multiple models defined!"
// #endif

#define I2C_SOFT_BUS_1
#define ACCEL_I2CBB1
#define ACCEL_KXTJ3

#define MIN_CALIBRATION_OFFSET 100 // Min value for calibration
#define SOLDERING_TEMP         320 // Default soldering temp is 320.0 °C
#define MAX_TEMP_C             450 // Max soldering temp selectable °C
#define MAX_TEMP_F             850 // Max soldering temp selectable °F
#define MIN_TEMP_C             10  // Min soldering temp selectable °C
#define MIN_TEMP_F             50  // Min soldering temp selectable °F
#define MIN_BOOST_TEMP_C       250 // The min settable temp for boost mode °C
#define MIN_BOOST_TEMP_F       480 // The min settable temp for boost mode °F

#define FILTER_DISPLAYED_TIP_TEMP   4


#define VOLTAGE_DIV        540 // 540 - Default divider from schematic
#define CALIBRATION_OFFSET 900 // 900 - Default adc offset in uV
#define POWER_LIMIT        60  // 60 watts default limit

#define USB_PD_VMAX        20 // Maximum voltage for PD to negotiate
#define MAX_POWER_LIMIT    100
#define POWER_LIMIT_STEPS  5
#define OP_AMP_GAIN_STAGE  57

#define HARDWARE_MAX_WATTAGE_X10 1000    // 100W

// #define OLED_I2CBB1    0
// #define TIPTYPE_T12    0 // Can manually pick a T12 tip

/// Active Disturbance Rejection Control - first order
// https://nl.mathworks.com/help/slcontrol/ug/active-disturbance-rejection-control.html
// LIMITATION: estimator coefficients (A, B) are precalculated for FIXED sample rate
//#define TIP_CONTROL_ARDC1
#define TIP_ARDC1_Kp            1.5
#define TIP_ARDC1_Kd            0.5
#define TIP_ARDC1_b0            0.085
#define TIP_ARDC1_A             {6.7996e-02, 1.8013e-02, -9.0063e-01, 9.6863e-01}
#define TIP_ARDC1_B             {TIP_ARDC1_b0*1.2609e-03/0.07, 9.3200e-01, TIP_ARDC1_b0*-2.1961e-03/0.07, 9.0063e-01}

/// Use PID Control
#define TIP_CONTROL_PID
#define TIP_PID_KP              40   // Matches proven Pinecilv2 value; higher drive under load
#define TIP_PID_KI              700  // Integral climb rate is proportional to error; 700 closes a small (2-3C) load-induced droop in ~10s. Far above the Pinecilv2 reference value (6) -- this is empirically tuned, not derived; watch for slow hunting, especially as the integral bleeds off after a heavy solder joint
#define TIP_PID_KD              8000 // C245 has a surprisingly high inertia, needs lot of dampening
#define TIP_PID_INTEGRAL_LIMIT_SCALE 30 // Default 5 caps integral at ~4.9W; 30 (~29W) covers measured heavy-load losses (XT60 at 430C draws 15-18W)

/// Default Control if no other controller is defined
#define TIP_THERMAL_MASS        0    // Not used for PID
#define TIP_THERMAL_INERTIA     0    // Not used for PID
#define TIP_RESISTANCE          25   // C245 is around 2.5R

#define FLASH_LOGOADDR      (0x08000000 + (122 * 1024)) // 2KB up to address 0x0801F000 (124 * 1024)
#define SETTINGS_START_PAGE (0x08000000 + (120 * 1024))

#define LCD_160x80          1
