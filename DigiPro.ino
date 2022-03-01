/***************************************************************************
 * Lora digipeater
 * 
 * Based on ExpressTracker code, using Lora module and Atmega 328 with
 * internal 8 MHz RC oscillator.
 * 
 * Note about using internal RC:
 * -Download MiniCore hardware lib for Arduino.
 * -Use Arduino as programmer and connect them to ICP header
 * -Configure Board with no bootloader, internal 8MHz and  hit burn bootloader 
 *  just to set configuration fuse. (default was RC internal 8/8 = 1 MHz.
 * -Burn code. Take note watchdog is enabled by default, take care of them.
 * 
 * V1.0 - AIG-4 code, working good.
 * V2.0 - Code increase above 16k (328 only)
 *      - Added BMP180 sensor for internal temp and pressure
 *      - Re-organize code and file, adding watchdog,ax25_util and project.h
 *      - AIG-4 (sept. 2021)
 * V2.1 -BUG: Lora module ne reset jamais. Library laisse la pin non defini.
 *      -Retarder tout les beacons sauf 1 pour pas qu'il transmettre tous en même temps.
 *      -Compilation conditionnel pour enlever le BMP180 et le OE et compiler pour 168P (<16k)
 *      -BUG: When CPU reboot on transmit high current when battery are very cold, sleep 
 *       beacon is transmitted immediatly after each reboot and digi enter a death loop. 
 * 
 * Calibrate:
 * Batt voltage, using define in DigiPro.
 * RF Frequency, using SDR receiver and set frequency centered.
 * 
 * Pinout:
 * See project.h 
 * 
 * Current draw:
 * 11.83ma on RX (95% is lora Module), 0.63ma when radio module sleeping 
 * using MCP1702-33. Quiescent current of this regulator is 5uA max. Cheap
 * 1117 regulator quiescent current is around 10ma, more than whole circuit.
 * 
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
#if DS_ENABLE==1
OneWire oneWire(DS_SENSOR); 
DallasTemperature sensors(&oneWire);
#endif

/* BMP180 INSIDE TEMPERATURE AND PRESSURE SENSOR */
#if BMP180_ENABLE==1
Adafruit_BMP085 bmp_sensor;
#endif


/******************************************
 * readVoltage()
 *****************************************/
#if VOLT_ENABLE==1
void readVoltage() {
	batt_volt = 0;
	for(uint8_t i=0; i<10; i++) batt_volt += analogRead(BATT_VOLT);
	batt_volt = (uint32_t)batt_volt * BAT_CAL / 10230L;
}
#endif

/******************************************
 * setup()
 *****************************************/
void setup() {

    /* CONFIGURE WATCHDOG FOR 1HZ INTERRUPT */
    Watchdog_setup();

    /* BATTERY VOLTAGE SENSOR */
    #if VOLT_ENABLE==1
    analogReference(INTERNAL);
    analogRead(BATT_VOLT);      // 1st reading seem a little bit off
	#endif
	
    /* IF MODEM CONFIGURE FAIL, RESET BOARD AT NEXT WATCHDOG IRQ */
    if(DigiInit() == 0) {
        wdt_flag = 31;
        while(1);       // Watchdog will reset
    }

    /* SENSOR INIT */ 
	#if DS_ENABLE==1
    sensors.begin();
	#endif
	#if BMP180_ENABLE==1
    bmp_sensor.begin();
	#endif
    #if VOLT_ENABLE==1
    readVoltage(); 
	#endif
}


/******************************************
 * loop()
 *****************************************/
void loop() {
    uint32_t t;
    static uint32_t sensor_to;

    /* GO TO SLEEP FOR 1 SECOND IF NO REQUEST FROM MODULE */
    if(digitalRead(LORA_DIO) == LOW) {

        /* ENABLE INTERRUPT ON DIO0 TO WAKE-UP FROM SLEEP */
        PCMSK2 = 0x08;    // PCINT19 pin-on-change interrupt for PD3
        PCICR = 0x04;     // PCINT2 ENABLE
    
        /* POWER DOWN CPU, WAKE-UP WITH 1Hz WATCHDOG INTERRUPT OR INCOMING PACKET */
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
    if(wdt_clk > sensor_to) {
        sensor_to = wdt_clk + 60;

		/* AVERAGE 10x ADC READ BATTERY VOLTAGE IN mV */
		#if VOLT_ENABLE==1
		readVoltage();
		#endif
		
		/* DS18B20 EXTERIOR TEMP */
		#if DS_ENABLE==1
        sensors.requestTemperatures();  
        ext_temp = sensors.getTempCByIndex(0); 
		#endif
		
		/* BMP180 INTERIOR TEMP AND PRESSURE */
		#if BMP180_ENABLE==1
        pressure = (float)bmp_sensor.readPressure() / 1000.0;    // Measure in kPa;
        int_temp = bmp_sensor.readTemperature();        
		#endif
    }

    /* SEND AND RECEIVE PACKET, UNTIL NOTHING TO DO */
    while(DigiPoll());

    /* GO TO SLEEP MODE IF BATTERY DROP BELOW A LEVEL */
    #if VOLT_ENABLE==1
    if(batt_volt < 3500) {
		
		/* SEND SLEEP BEACON ONCE, AND ONLY IF CPU NOT REBOOTED ON LOW BATTERY ON TRANSMIT */
        if(sleep_flag==0 && wdt_clk>600) {
			lora.setPower(13);		// 20mW beacon 
			sleep_flag = 1;		
			DigiSendBeacon(2);    	// send system beacon for sleep mode
			lora.setPower(LORA_POWER);
		}        
        DigiSleep();          // Put lora radio module in sleep
        
        /* WAIT 15 MINUTES */
        t = wdt_clk + (60 * 15);
        while(wdt_clk < t) {
            wdt_flag = 0;
            set_sleep_mode(SLEEP_MODE_PWR_DOWN);
            sleep_enable();
            sleep_mode();
            sleep_disable();
        }
                
    } else sleep_flag = 0; 
	#endif
}
