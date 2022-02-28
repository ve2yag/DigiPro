/***************************************************************************
 * Lora digipeater
 * 
 * Based on ExpressTracker code, using Lora module and Atmega 168(328PB) with
 * internal 8 MHz RC oscillator.
 * 
 * Note about using internal RC:
 * -Download MiniCore hardware lib for Arduino.
 * -Burn ArduinoISP into board, connect SPI/Reset to target with Mosfet
 *  level converter.
 * -Make sure Lora module have pull-up on CS, because pin are Hi-Z when 
 *  programming.
 * -Configure Board with no bootloader, but hit burn bootloader just to
 *  set configuration fuse. (default was RC internal 8/8 = 1 MHz.
 * -Burn code. Take note watchdog is enabled by default, take care of them.
 * -To setup watchdog, MCUSR must be cleared, else watchdog cannot be stopped.
 * 
 * V1.0 - AIG-4 code, working good.
 * V2.0 - Code increase above 16k (328 only)
 *      - Added BMP180 sensor for internal temp and pressure
 *      - Re-organize code and file, adding watchdog,ax25_util and project.h
 *      - AIG-4 (sept. 2021)
 * V2.1 -BUG: Lora module ne reset jamais. Library laisse la pin non defini.
 *      -Retarder tout les beacons sauf 1 pour pas qu'il transmettre tous en même temps.
 *      -Compilation conditionnel pour enlever le BMP180 et le OE et compiler pour 168P (<16k)
 *      -Trap packet trop court et packet sans path final bit ou au mauvais endroit.
 * 		-BUG: Sleep packet send each 2 minutes. (fix: send only once when battery become low)
 *      -setup() Wait for voltage rise above 3.5v or sleep beacon is sleep non-stop
 *      -BUG: WaitClear() is bypass. re-enable it.
 *      -Change CR47 to CR45 to align with Lora in europa.
 * 
 * Calibrate:
 * Batt voltage, using define in DigiPro.
 * RF Frequency, using SDR receiver and set frequency centered.
 * 
 * Pinout:
 * See project.h 
 * 
 * Note:
 * AREF - Capactor to ground ***** NOT TO VCC !!!!!!!!!
 * 
 * Current draw:
 * Test board draw 61ma on TX 17dbm, 23ma on rx, with led without power save
 * With power save, 11.83ma on RX, 0.63ma when radio module sleeping. KY5033 reg.
 * 
 ***************************************************************************/
#include <avr/sleep.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <Adafruit_BMP085.h>

#include "project.h"

/* TELEMETRY */
uint16_t batt_volt;
float ext_temp;
float int_temp,pressure;

bool sleep_flag;

/* EXTERIOR TEMPERATURE SENSOR */ 
OneWire oneWire(DS_SENSOR); 
DallasTemperature sensors(&oneWire);

/* BMP180 INSIDE TEMPERATURE AND PRESSURE SENSOR */
#if BMP180_ENABLE==1
Adafruit_BMP085 bmp_sensor;
#endif

/******************************************
 * setup()
 *****************************************/
void setup() {

    /* CONFIGURE WATCHDOG FOR 1HZ INTERRUPT */
    Watchdog_setup();

    /* BATTERY VOLTAGE SENSOR */
    analogReference(INTERNAL);
    analogRead(BATT_VOLT);      // 1st reading seem a little bit off

    /* IF MODEM CONFIGURE FAIL, RESET BOARD AT NEXT WATCHDOG IRQ */
    if(DigiInit() == 0) {
        wdt_flag = 31;
        while(1);       // Watchdog will reset
    }
	DigiSleep();		// Put in low power mode until end of setup

	/* ON REBOOT, HOLD LORA RESET AND WAIT FOR VOLTAGE RISE ABOVE 3.5 VOLTS */
	do {
		/* SLEEP FOR ONE SECOND */
        wdt_flag = 0;
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sleep_mode();		
        sleep_disable();
        
        /* AVERAGE 10x ADC READ BATTERY VOLTAGE IN mV */
        batt_volt = 0;
        for(uint8_t i=0; i<10; i++) batt_volt += analogRead(BATT_VOLT);
        batt_volt = (uint32_t)batt_volt * BAT_CAL / 10230L;
        
	} while(batt_volt < 3500);

    /* SENSOR INIT DS1820 AND BMP180 */ 
    sensors.begin();
#if BMP180_ENABLE==1
    bmp_sensor.begin(); 
#endif
}


/******************************************
 * loop()
 *****************************************/
void loop() {
    uint32_t t;
    static uint32_t temp_to;

    /* GO TO SLEEP FOR 1 SECOND IF NO REQUEST FROM MODULE */
    if(digitalRead(LORA_DIO) == LOW) {

        /* ENABLE INTERRUPT ON DIO0 TO WAKE-UP FROM SLEEP */
        PCMSK2 = 0x08;    // PCINT19 pin-on-change interrupt for PD3
        PCICR = 0x04;     // PCINT2 ENABLE
    
        /* CPU GO TO SLEEP FOR 1 SEC OR INCOMING PACKET */
        wdt_flag = 0;
        set_sleep_mode(SLEEP_MODE_PWR_DOWN);
        sleep_enable();
        sleep_mode();		// Watchdog wake CPU or DIO0 rising level, set in SX1278.CPP
        sleep_disable();

        /* DISABLE DIO0 INTERRUPT */
        PCICR = 0;
        PCMSK2 = 0;
    }

	/* CHECK SENSOR EACH MINUTE */
    if(wdt_clk > temp_to) {
        temp_to = wdt_clk + 60;

		/* AVERAGE 10x ADC READ BATTERY VOLTAGE IN mV */
		batt_volt = 0;
		for(uint8_t i=0; i<10; i++) batt_volt += analogRead(BATT_VOLT);
		batt_volt = (uint32_t)batt_volt * BAT_CAL / 10230L;

		/* DS18B20 EXT TEMP */
        sensors.requestTemperatures();  
        ext_temp = sensors.getTempCByIndex(0); 

		/* BMP180 INT TEMP AND PRESSURE */
		#if BMP180_ENABLE==1
        pressure = (float)bmp_sensor.readPressure() / 1000.0;    // Measure in kPa;
        int_temp = bmp_sensor.readTemperature();        
		#endif
    }

    /* SEND AND RECEIVE PACKET, UNTIL NOTHING TO DO */
    while(DigiPoll());

    /* GO TO SLEEP MODE 2 MINUTES IF BATTERY DROP BELOW A LEVEL */
    if(batt_volt < 3500) {
        if(sleep_flag==0) DigiSendBeacon(2);    // send system beacon for sleep mode
        sleep_flag = 1;
        DigiSleep();          // Put lora radio module in sleep mode
        t = wdt_clk + (60 * 2);
        while(wdt_clk < t) {
            wdt_flag = 0;
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_enable();
            sleep_mode();
            sleep_disable();
        }
                
    } else sleep_flag = 0; 
}
