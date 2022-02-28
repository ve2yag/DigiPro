
#ifndef DIGI_H 
#define DIGI_H

/* Transmit digipeater beacon */
int DigiInit();
void DigiSleep();
int DigiWake();
int DigiPoll();
void DigiSendBeacon(uint8_t id);

#endif
