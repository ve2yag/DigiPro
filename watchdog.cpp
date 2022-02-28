
#include "project.h"

#include <avr/wdt.h>
#include <avr/interrupt.h>

/* 1 HZ TICK COUNTER, REPLACING MILLIS() BECAUSE CPU USE POWER DOWN */
volatile uint32_t wdt_clk;

/* CLEAR THIS COUNTER TO TRIG THE WATCHDOG, AFTER 30 SEC, THE BOARD RESET ITSELF */
volatile uint8_t wdt_flag;

/******************************************************
 * PCINT2 interrupt vector 
 * (for pin interrup PCINT16-PCINT23)
 *****************************************************/
ISR(PCINT2_vect) { 
} 

/******************************************************
 * Watchdog IRQ
 * 
 * -Provide 1 Hz clock (wdt_clk)
 * -Provide 30 sec watchdog if wdt_flag not cleared.
 * -Reset board every 24h
 ******************************************************/
ISR(WDT_vect) {
    wdt_flag++;
    wdt_clk++;
    if(wdt_flag > 30 || wdt_clk > WD_REBOOT_VALUE) {
        WDTCSR |= (1<<WDCE) | (1<<WDE);
        WDTCSR = (1<<WDE);        // Enable watchdog reset, timeout 16ms.
        while(1);                 // Wait CPU reset
    } 
    wdt_reset();     
}

/******************************************
 * Watchdog_setup
 * 
 * Configure watchdog for 1 Hz IRQ
 *****************************************/
void Watchdog_setup() {  
    cli();
    MCUSR = 0;
    wdt_reset();

  /* RESET VARIABLE */
    wdt_clk=0;
    wdt_flag=0;
    
    /* Start timed equence */
    WDTCSR |= (1<<WDCE) | (1<<WDE);
    
    /* WATCHDOG INTERRUPT 1 SEC */
    WDTCSR = (1<<WDIE) | (1<<WDP2) | (1<<WDP1);
    sei();
}
