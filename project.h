
#include <Arduino.h>

#include "digi.h"
#include "sx1278.h"
#include "watchdog.h"
#include "ax25_util.h"

/*
 * When using L as primary table symbol, here symbol ID icon:
 * 'a' is red losange 
 * '>' is car 
 * 'A' is white box 
 * 'H' is orange box 
 * 'O' is baloon 
 * 'W' is green circle 
 * '[' is person 
 * '_' is blue circle
 * 'n' is red triangle 
 * 's' is boat 
 * 'k' SUV 
 * 'u' is Truck 
 * 'v' is Van
 * 'z' is Red house
 */

/* BATTERY ADC CALIBRATION */
#define BAT_CAL 4558L   // (float)batt_volt * BAT_CAL / 1023  In millivolts

/* WATCHDOG DAILY REBOOT VALUE (86400 is around 28h, not 24) */
#define WD_REBOOT_VALUE 74060L

/* LORA RADIO PARAMETER */
#define FREQ 433.775      // TX freq in MHz
#define FREQ_ERR -24000	  // Freq error in Hz
#define LORA_POWER 20     // Power of radio (dbm)
#define PPM_ERR lround(0.95 * (FREQ_ERR/FREQ))  // 25khz offset, 0.95*ppm = 25Khz / 433.3 = 57.65 * 0.95 = 55

/* RADIO CHANNEL COLLISION */
#define CHANNEL_SLOTTIME 100  /* 100ms slottime */
#define CHANNEL_PERSIST 63    /* 25% persistance */

/* DIGIPEATER CONFIG */
#define OE_TYPE_PACKET_ENABLE 1		// Enable ASCII and binary dual mode 
//#define MYCALL   "NOCALL"		// Put your call here
#define BCN_DEST "APZDG2-1"		// -1 -2 or -3 for SSID digipeating else:
#define BCN_PATH ""				// Set to "" to disable,  and "WIDE2-2" for std path
//#define BCN_POSITION PSTR("!4100.00NL07000.00Wa") // Put your position here
#define B1_COMMENT PSTR("433.775 MHz 20dbm B125 SF12 CR45")
#define B2_COMMENT PSTR("Lora digi V2.1") 
#define B1_INTERVAL    1800 
#define B2_INTERVAL    1550
#define B3_INTERVAL    1750
#define TELEM_INTERVAL 950
#define WIDEN_MAX      3

/* HARDWARE SENSOR CONFIG */
#define DS_ENABLE           1
#define BMP180_ENABLE       1
#define VOLT_ENABLE         1

/* FRAME DUPLICATE TABLE CONFIG */
#define DUP_DELAY 40          /* Delay in sec to keep frame in memory */
#define DUP_MAXFRAME 5        /* Maximum duplicate frame memory */

/* PIN DEFINITION */
#define RXD_GPS    0  // UBlox GPS (Only with tracker)
#define TXD_GPS    1
#define LORA_CS    2  // Lora radio module
#define LORA_DIO   3  // Lora radio module
#define DS_SENSOR  4  // DS18B20 external temp sensor   
#define LORA_MOSI  11 // Lora radio module
#define LORA_MISO  12
#define LORA_SCLK  13
#define LORA_RESET 17 
#define I2C_SDA    18 // BMP180 sensor 
#define I2C_SCL    19
#define BATT_VOLT  A0 // (A0 for DIP28 prototype, else A7 for TQFP-32 PCB) Cell voltage, 15k/47k 5v = 1.22v (internal 1.2v ref)

/* TELEMETRY */
extern uint16_t batt_volt;
extern float int_temp,pressure;
extern float ext_temp;

/* SET WHEN BOARD ARE UNDER SLEEP MODE */
extern bool sleep_flag;
extern SX1278 lora;			// Digipro.ino, to set low power on sleep beacon

