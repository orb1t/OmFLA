/*
    Copyright (C) 2018  Dr. Jürgen Sauermann

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __USER_DEFINED_PARAMETERS_DEFINED__
#define __USER_DEFINED_PARAMETERS_DEFINED__

//=============================================================================
// ======== USER-CONFIGURABLE PARAMETERS ======================================
//=============================================================================

// CPU calibration is needed if:
//
// 1. serial communication is used (i.e. for the debug print and enocean
//    targets), AND
//
// 2. the internal RC oscillator is used and its frequency is not close to
//    an integer multiple of 16 times the highest baudrate used (i.,e. 57600)
//
// Most ordinary mortals are expected to use the no-debug tartget and
// therefore do not need any CPU calibration.
//
// A more elegant approach is to use a 3,6864 MHz crystal instead of the
// internal 4 MHz RC oscillator. In that case the crystal frequency is exactly
// 4*16*57600 and no calibration isd needed. The downside is probably a
// slightly higher current consumption.

enum CPU_Calibration
{
   NO_CPU_CALIBRATION = 0xFF,   // no CPU calibration
// CPU_CALIBRATION    = NO_CPU_CALIBRATION   // good luck!
   CPU_CALIBRATION    = 0x46    // Ralf
// CPU_CALIBRATION    = 0x3F    // PCB #2
};

/// alarm thresholds define at which glucose level an alarm is raised
enum Alarm_thresholds
{
   ABSOLUTE_HIGH   = 250,   // mg/dl, beep if glucose > ABSOLUTE_HIGH
   ABSOLUTE_LOW    =  80,   // mg/dl, beep if glucose < ABSOLUTE_LOW
   RELATIVE_HIGH   =  70,   // mg/dl, beep if glucose > initial + RELATIVE_HIGH
   RELATIVE_LOW    =  70,   // mg/dl, beep if glucose < initial - RELATIVE_LOW
};

/// how the raw 12-bit glucose sensor value translate to glucose in mg/dl
// the formula used is:
//
// glucose = SENSOR_OFFSET + SENSOR_SLOPE/1000 * raw_sensor_value
//
//
enum Sensor_Calibration
{
   SENSOR_SLOPE  = 130,   // uint8_t / 1000 = (0.13)
   SENSOR_OFFSET = 236    // int8_t         = -20
};

/// how battery levels map to beep counts (at power-on of the device)
enum Battery_levels
{
   BATTERY_1 = 1200,   // beep once (battery is low)
   BATTERY_2 = 1100,
   BATTERY_3 = 1000,
   BATTERY_4 =  900,
   BATTERY_5 =  800,   // beeep 5 times (battery is full)
};

//=============================================================================
// ======== END OF USER-CONFIGURABLE PARAMETERS ===============================
//=============================================================================

#include <stdint.h>

struct User_defined_parameters
{
   // some parameters are shifted so that they fint into a uint8_t
   //
   uint8_t oscillator_calibration;   //
   uint8_t sensor_slope;             //
    int8_t sensor_offset;            //
   uint8_t abs_HIGH__2;              // >> 1
   uint8_t abs_LOW__2;               // >> 1
   uint8_t rel_HIGH__2;              // >> 1
   uint8_t rel_LOW__2;               // >> 1
   uint8_t battery_1__8;             // >> 3
   uint8_t battery_2__8;             // >> 3
   uint8_t battery_3__8;             // >> 3
   uint8_t battery_4__8;             // >> 3
   uint8_t battery_5__8;             // >> 3
};

#endif // __USER_DEFINED_PARAMETERS_DEFINED__
