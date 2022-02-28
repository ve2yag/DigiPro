#ifndef WD_H 
#define WD_H

/* WATCHDOG CLOCK 1HZ */
extern volatile uint32_t wdt_clk;

/* CLEAR THIS COUNTER TO TRIG THE WATCHDOG, AFTER 10 SEC, THE BOARD RESET ITSELF */
extern volatile uint8_t wdt_flag;

void Watchdog_setup();

#endif
