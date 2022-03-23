
#include "project.h"

/* LORA MODULE, CONFIG OVERWRITED BY SETTING IN PROJECT.H */
SX1278 lora(SX1278_BW_125_00_KHZ, SX1278_SF_12, SX1278_CR_4_5);

/* TELEMETRY CONFIG */
const char TelemSequence[] = { 1,1,1,4,1,1,1,3,1,1,1,2,1,1,1,0 }; 

/* PAYLOAD BUFFER */
static unsigned char pkt[255], index;
bool pkt_oe_format;

/* Duplicate frame table */
struct TDupFrame {
    unsigned long time;    // Time when receive packet (if 0, empty slot)
    uint16_t crc;          // CRC of data block
} TDupFrame;

struct TDupFrame DupFrame[DUP_MAXFRAME];

/* Beacon timer */
uint32_t Beacon1Timer;     // Value of Timer for next beacon transmission
uint32_t Beacon2Timer; 
uint32_t Beacon3Timer;     // System beacon (Version and up-time)
uint32_t TelemTimer;       // Telemetry timer 

// STAT
unsigned int stat_rx_pkt, stat_digipeated_pkt, stat_tx_pkt;
unsigned int stat_oe_pkt, stat_bin_pkt;  


/******************************************************************************
 * Check timer overflow
 *****************************************************************************/
long TimerOverflow(unsigned long value) {
    return (long)(value-wdt_clk) < 0 ? 1:0;
}   


/******************************************************************************
 * Watch clear channel, 100ms slottime, persistance 63.
 *****************************************************************************/
void WaitClearChannel() {
    uint32_t t;

    do {
        t = millis() + CHANNEL_SLOTTIME;      
        do {
            if(lora.rxBusy()) t = millis() + CHANNEL_SLOTTIME;      
        } while(millis() < t);          
    } while(random(0,256) > CHANNEL_PERSIST);
}


/******************************************************************************
 * Packet handling fonction
 * 
 * Create and manage packet.
 *****************************************************************************/
void CreatePacket() {
    uint8_t i;
            
    /* SEND SOURCE/DEST CALLSIGN */  
    index=0;
    asc2AXcall(BCN_DEST, &pkt[index]); 
    asc2AXcall(MYCALL, &pkt[index+7]); 
    index+=14;   

    /* PATH (ONLY ONE SUPPORTED) */
	if(strlen(BCN_PATH)!=0) {
	    asc2AXcall(BCN_PATH, &pkt[index]); 
		index+=7;
	}
	
    /* UI FRAME AND PID */
    pkt[index-1] |= 1;  // Finalize path here
    pkt[index++] = 0x03;    /* UI Frame */
    pkt[index++] = 0xF0;    /* PID */                                                      
}


/******************************************************************************
 * void SendPacket()
 * 
 * Wait channel to be clear and send packet.
 *****************************************************************************/
void SendPacket() {

	/* SEND IN ASCII OR BINARY, CHOOSE FORMAT THE MOST USED ON NETWORK AROUND */
	#if OE_TYPE_PACKET_ENABLE==1
    if(stat_oe_pkt>stat_bin_pkt) {
		char *buf = (char*)malloc(256);
		if(buf) {
			buf[0] = '<'; 
			buf[1] = 0xFF; 
			buf[2] = 0x01; 			
			DecodeAX25(pkt, index, &buf[3]);
			WaitClearChannel();
			lora.tx((uint8_t*)buf, strlen(&buf[3])+3);
			while(lora.txBusy());
			stat_tx_pkt++;
			free(buf);
			return;
		}
	}
	#endif
	
	/* WAIT CHANNEL CLEAR AND SEND BEACON */
    WaitClearChannel();
    lora.tx(pkt, index);
    while(lora.txBusy());
    stat_tx_pkt++;
}


/******************************************************************************
 * void DigiSendBeacon(uint8_t id)
 * 
 * Create and send digipeater beacon to Lora radio.
 *****************************************************************************/
void DigiSendBeacon(uint8_t id) {

    /* CREATE NEW PACKET */
    CreatePacket();
    
    /* SYSTEM STATUS BEACON */
    if(id == 2) {
        char tmp[6];
        dtostrf(ext_temp, 5, 1, tmp);
        index += sprintf((char*)&pkt[index], ">%umV (%s) T=%sC R%uD%uT%u", batt_volt, sleep_flag?"SLP":"ACT", tmp, stat_rx_pkt, stat_digipeated_pkt, stat_tx_pkt);
    } else {
      
        /* LATITUDE, TABLE/OVERLAY, LONGITUDE AND SYMBOL */
        index += sprintf_P((char*)&pkt[index], BCN_POSITION);  		// YAG-4 test site
 
        /* COMMENT */
        switch(id) {
            case 0:  index += sprintf_P((char*)&pkt[index], B1_COMMENT);  break; 
            case 1:  index += sprintf_P((char*)&pkt[index], B2_COMMENT); break; 
        }
    }

    SendPacket();
}


/******************************************************************************
 * void DigiSendTelem()
 * 
 * Send telemetry to radio.
 *****************************************************************************/
void DigiSendTelem() {
    uint8_t i, spc_count;
    static unsigned char seq,seq_cnt;    
    
     /* CREATE NEW PACKET */
    CreatePacket();
 
    /* CREATE APRS MESSAGE HEADER ONLY FOR TELEMETRY PARAMETERS */
    if(TelemSequence[seq]!=1) index += sprintf((char*)&pkt[index], ":%-9s:", MYCALL);
     
    /* FINISH TELEM PACKET */
    uint8_t param1 = ((float)batt_volt/1000.0 - 2.5) / 0.008;
    uint8_t param2 = (ext_temp + 60.0) / 0.5;
    uint8_t param3 = (int_temp + 60.0) / 0.5;
    uint8_t param4 = (pressure - 90.0) * 10.0;    // Pressure range 90-115 in 0.1 step
    
    switch(TelemSequence[seq++]) {
        case 1:  index += sprintf_P((char*)&pkt[index], PSTR("T#%03u,%03u,%03u,%03u,%03u,000,00000000"), seq_cnt++, param1, param2, param3, param4);  break; 
        case 2:  index += sprintf_P((char*)&pkt[index], PSTR("PARM.Vbatt,ExtT,IntT,Pres")); break;
        case 3:  index += sprintf_P((char*)&pkt[index], PSTR("UNIT.Volt,C,C,kPa")); break;
        case 4:  index += sprintf_P((char*)&pkt[index], PSTR("EQNS.0,0.008,2.5,0,0.5,-60,0,0.5,-60,0,0.1,90")); break;
    }

    /* RESET SEQUENCE AT END AND SET FINAL PACKET SIZE */
    if(TelemSequence[seq]==0) seq=0;

    SendPacket();
}


/******************************************************************************
 * unsigned short DoCRC(unsigned short crc, unsigned char c)
 * 
 * Compute CRC.
 *****************************************************************************/
unsigned short DoCRC(unsigned short crc, unsigned char c) {
    unsigned short xor_int;

    for (uint8_t i=0; i<8; i++) {       
        xor_int = crc ^ (c&1);
        crc>>=1;
        if(xor_int & 0x0001) crc ^= 0x8408;
        c >>=1;                  
    }

    return crc;
}


/******************************************************************************
* TestDup
*
* Return TRUE if packet given is already in duplicate table. Clear also old
* frame.
******************************************************************************/
int TestDup(unsigned char *p, int size) {
    uint8_t i,flag;
    uint16_t pkt_crc=0xFFFF;

    /* COMPUTE CRC*/
    for(i=0; i<size; i++) pkt_crc = DoCRC(pkt_crc, p[i]);
    
    /* CHECK FOR DUPLICATE, CLEAR OLD FRAME */
    for(i=0,flag=0; i<DUP_MAXFRAME; i++) {
        if(TimerOverflow(DupFrame[i].time)) DupFrame[i].time=0;  /* Clear old frame */      
        if(DupFrame[i].time==0) continue;                        /* Skip if slot is empty */
        if(DupFrame[i].crc==pkt_crc) flag=1;                     /* !! Find duplicate packet */
    } 

    return flag;
}


/******************************************************************************
* AddDupList
*
* Add packet to duplicate table. If table is full, oldest is deleted and
* replaced by the new.
*
* Only Data field is keeped for comparaison. Only UI frame.
******************************************************************************/
void AddDupList(unsigned char *p, int size) {
    uint8_t i,old;
    uint32_t oldest_time;
    uint16_t pkt_crc=0xFFFF;

    /* COMPUTE CRC*/
    for(i=0; i<size; i++) pkt_crc = DoCRC(pkt_crc, p[i]);

    /* PLACE FRAME INTO FIRST EMPTY SLOT */
    for(i=0; i<DUP_MAXFRAME; i++) {
        if(DupFrame[i].time==0) {
            DupFrame[i].time = wdt_clk + DUP_DELAY;
            DupFrame[i].crc = pkt_crc;
            return;
        }
    }

    /* LIST IS FULL, FIND OLDEST FRAME */
    old = 0;
    oldest_time = DupFrame[0].time;
    for(i=1; i<DUP_MAXFRAME; i++) {
        if( (long)(DupFrame[i].time-oldest_time)<0 ) {
            old = i;
            oldest_time = DupFrame[i].time;
        }
    }

    /* REPLACE OLDEST FRAME BY NEW */
    DupFrame[old].time = wdt_clk + DUP_DELAY;
    DupFrame[old].crc = pkt_crc;
}


/******************************************************************************
* DigiRepeat
* 
* Send digipeated packet and update stat
******************************************************************************/
void DigiRepeat(unsigned char *packet, int packet_size) {

	/* REPLY IN SAME FORMAT AS RECEIVED. ASCII OR BINARY */
	#if OE_TYPE_PACKET_ENABLE==1
    if(pkt_oe_format == true) {
		char *buf = (char*)malloc(256);
		if(buf) {
			buf[0] = '<'; 
			buf[1] = 0xFF; 
			buf[2] = 0x01; 			
			DecodeAX25(packet, packet_size, &buf[3]);
			WaitClearChannel();
			lora.tx((uint8_t*)buf, strlen(&buf[3])+3);
			while(lora.txBusy());
			stat_digipeated_pkt++;
			free(buf);
			return;
		}
	}
	#endif

	/* WAIT CHANNEL CLEAR AND SEND BEACON */
    WaitClearChannel();
    lora.tx(packet, packet_size);
    while(lora.txBusy());
    stat_digipeated_pkt++;
}


/******************************************************************************
 * void MessageHandler(unsigned char *buf, size)
 * 
 ******************************************************************************/
void MessageHandler(unsigned char *buf, uint8_t size) {
	
	/* QUERY STATUS */
	if(memcmp_P(buf, PSTR("?APRSS"), 6) == 0) {
		Beacon3Timer=wdt_clk;
		return;
	}
}


/******************************************************************************
 * void DigiRules(unsigned char *packet, int packet_size)
 * 
 * Apply digipeater rule to packet and digipeat if needed.ex
 *
 * Buffer Format:
 * Destination call : 6 byte + 1 bytes (CALL + SSID) SSID bit 4:1
 * Source call      : 6 byte + 1 bytes (CALL + SSID)
 * Path             : 6 byte + 1 bytes (CALL + SSID)   
 *                    ... up to 7 digipeting path, end with bit 0 of SSID set
 * Control          : 1 byte (must be UI frame)
 * PID              : 1 byte (don't care)                                              
 * Data frame       : variable
 * 
 * Rule:
 * -Reject any non-UI frame
 * -Reject packet from this node (Source call = Node call)
 * -Trig beacon 1 if data frame contain ?APRS?
 * -Trig beacon 3 if data frame contain :<nodecall>:?APRSS
 * -Test if packet is in duplicate list
 * -Process generic SSID digipeating
 * -Reject if no path
 * -Test for WIDEn-n
 ******************************************************************************/
void DigiRules(unsigned char *packet, uint8_t packet_size) {
    uint8_t DataIndex,PathIndex,i;  
    unsigned char flag,ssid,c; 
    unsigned char NodeCall[7];
    char tmp[12]; 
    
    /* REJECT NON-UI FRAME, FIND DATA FRAME (DataIndex) */
    for(DataIndex=0; DataIndex<packet_size; DataIndex++) if(packet[DataIndex]&1) break;
    if(packet[++DataIndex]!=0x03) return;   
    DataIndex+=2;   /* Skip PID */
    
    /* TEST FOR PACKET FROM THIS NODE */
	asc2AXcall(MYCALL, NodeCall);
    for(i=0, flag=0; i<7; i++) { 
        if(i!=6) {
            if(packet[7+i]!=NodeCall[i]) { flag=1; break; }   
        } else {
            if((packet[7+i]&0x1E)!=(NodeCall[i]&0x1E)) { flag=1; break; }   
        }
    }
    if(flag==0) return; 

    /* TEST FOR DUPLICATE PACKET */
    if(TestDup(&packet[DataIndex], packet_size-DataIndex)) return; 

	/* CHECK MESSAGE FOR THIS STATION */
	sprintf(tmp, ":%-9s:", MYCALL);
	if(memcmp(&packet[DataIndex], tmp, 11) == 0) {

		/* PROCESS MSG */
		MessageHandler(&packet[DataIndex+11], packet_size-11-DataIndex);
		
		/* REPLY */
		for(uint8_t i=DataIndex; i<packet_size; i++) {
			if(packet[i]=='{') {			// Scan for ACK number
						
				/* GET TAG */
				int tag=0;
				i++;   
				while(isdigit(packet[i]) && i<packet_size) {    
					tag*=10; 
					tag+=(packet[i++]-48);   
				}

				/* GET SOURCE CALLSIGN */
				char *call = AXCall2asc(&packet[7]);
			
				/* CREATE PACKET */
				CreatePacket();
				index += sprintf((char*)pkt+index,":%-9s:ack%u",call,tag);
                SendPacket();            		
				return;
			}	    
		}
		return;
	}
	
    /* TEST FOR ?APRS? QUERY */
    for(i=0, flag=0; i<6; i++) if(packet[i+DataIndex]!="?APRS?"[i]) { flag=1; break; }
    if(flag==0) Beacon1Timer=wdt_clk; 
  
    /* TEST FOR DEST SSID DIGIPEATING */ 
    ssid = (packet[6]&0x1E)>>1;
    if(ssid!=0 && ssid<=WIDEN_MAX) {
		
		/* DECREMENT DEST SSID AND ADD TO DUP LIST */
        packet[6] = (packet[6]&0xE1) | ((ssid-1)<<1);   // Decrement SSID
        AddDupList(&packet[DataIndex],packet_size-DataIndex);
        
        /* SEARCH WHERE TO INSERT DIGI CALL */
        PathIndex = 14;
		while((packet[PathIndex-1]&1)==0 && (packet[PathIndex+6]&128)!=0) PathIndex+=7;    

        /* MAKE ROOM FOR DIGICALL */
		for(i=(packet_size-1); i>=PathIndex; i--) packet[i+7] = packet[i];

        /* COPY DIGI CALL TO PATH */
        memcpy(&packet[PathIndex], NodeCall, sizeof(NodeCall));
		packet[PathIndex+6] |= (packet[PathIndex-1]&1) | 0x80;	// Move end-of-path bit + set has-been-repeated bit 
		packet[PathIndex-1] &= 0xFE;							// Clear end-of-path bit of previous path
        packet_size+=7;
        
        /* DIGIPEAT THEM */
        DigiRepeat(packet, packet_size);  
        return;
    }

    /* REJECT PACKET IF NO PATH */
    if(packet[13]&1) return;

    /* TEST PATH FOR WIDEn-n */
    PathIndex = 14;
    while(1) {     
        if( (packet[PathIndex+6]&0x80) == 0) { 
                    
            /* GET SSID */
            ssid = (packet[PathIndex+6]&0x1E)>>1; 
            flag=0;
            for(i=0; i<4; i++) { if(packet[PathIndex+i]!=("WIDE"[i])<<1) flag=1; } /* Call must be WIDE */
            if(ssid==0 || ssid>WIDEN_MAX) flag=1;  /* ssid must be 1 to wide-n maximum */
            c = packet[PathIndex+4]>>1;
            if(c<'1' || c>('0'+WIDEN_MAX)) flag=1;  /* Test WIDEn : n must be between 1 and maximum */

            if(flag==0) {
                ssid--;                         /* decrement SSID */
                packet[PathIndex+6] = (packet[PathIndex+6]&0xE1) | (ssid<<1);   
 
                /* MAKE ROOM FOR DIGICALL IF SSID IS NOT 0 */
                if(ssid) {
                    for(i=(packet_size-1); i>=PathIndex; i--) packet[i+7] = packet[i];
                    packet[PathIndex+6] = 0; // Clear end of path bit
                    DataIndex+=7;
                    packet_size+=7;
                }

                /* COPY DIGI CALL TO PATH  */
                for(i=0; i<6; i++) packet[PathIndex+i] = NodeCall[i];
                packet[PathIndex+6] = (packet[PathIndex+6]&0xE1) | (NodeCall[6]&0x1E);
                packet[PathIndex+6] |= 0x80;     /* Set has-been-repeated bit */   

                /* DIGIPEAT IT */
                AddDupList(&packet[DataIndex],packet_size-DataIndex);
                DigiRepeat(packet, packet_size);
                return;  
            }                                                               
            return;   // If no rules apply to current digi path, exit now.
        }
    
        if(packet[PathIndex+6]&1) break;       // Stop at end of path 
        PathIndex+=7;
    } 
}


/******************************************************************************
 * void DigiPoll()
 *
 * Check if beacon are timeout, transmit.
 *****************************************************************************/
int DigiPoll() {
    static uint8_t status, length, i, c;
    static char *payload;
    //TAX25Frame *ax25_frame;
    
    status = lora.rxAvailable(pkt, &length);
    if(status==ERR_NONE) {

        /* REMOVE TOO SHORT PACKET 7+7(SRC/DEST) + 2(UI/PID) + 1(DATA) */
        if(length<17) return 1;

        /* IF PACKET ARE ASCII, CONVERT THEM BEFORE HEADER IS: < 0xFF 0x01 */
		#if OE_TYPE_PACKET_ENABLE==1
		pkt_oe_format = false;
        if(pkt[0] == '<' && pkt[1] == 0xFF) {
			payload = (char*)malloc(255);
			if(payload==0) return 0;
            memset(payload, 0, 255);
            memcpy(payload, &pkt[3], length-3);
            length = EncodeAX25(payload, pkt);
            free(payload);
            pkt_oe_format = true;
            stat_oe_pkt++;
        } else {
			stat_bin_pkt++;
		}
		#endif

        /* SPOT CHECK FOR BAD PACKET, CHECK ADDRES FINAL BIT */
        for(i=0; i<length; i++) { c=pkt[i]; if((c & 1) == 1) break; }  // Search for path final bit
        if((c&1) == 0) return 1;                                       // Abort if no final bit
        if(((i+1)%7) != 0) return 1;                                   // Abort if final bit position not call field aligned
        if(i==6) return 1;                                             // Abort if final bit are too early on header
              
        /* DIGIPEAT AX25 PACKET */
        stat_rx_pkt++;
        DigiRules(pkt, length);
        return 1;
    }

    /* BEACON 1 TIMEOUT */
    if(TimerOverflow(Beacon1Timer)!=0 ) { 
        DigiSendBeacon(0);
        Beacon1Timer = wdt_clk + (uint32_t)B1_INTERVAL;
        return 1;
    }

    /* BEACON 2 TIMEOUT */
    if(TimerOverflow(Beacon2Timer)!=0 ) {
        DigiSendBeacon(1);
        Beacon2Timer = wdt_clk + (uint32_t)B2_INTERVAL;
        return 1;
    }

    /* BEACON 3 TIMEOUT */
    if(TimerOverflow(Beacon3Timer)!=0 ) {
        DigiSendBeacon(2);
        Beacon3Timer = wdt_clk + (uint32_t)B3_INTERVAL;
        return 1;
    }

    /* TELEMETRY TIMEOUT */
	#if VOLT_ENABLE==1 || BMP180_ENABLE==1 || DS_ENABLE==1
    if(TimerOverflow(TelemTimer)!=0) {
        DigiSendTelem();
        TelemTimer = wdt_clk + (uint32_t)TELEM_INTERVAL; 
        return 1;
    }
	#endif
	
    return 0; 
}


/******************************************************************************
 * void DigiSleep()
 *
 * Put radio module in sleep.
 *****************************************************************************/
void DigiSleep() {
    lora.end();  
}


/******************************************************************************
 * void DigiWake()
 *
 * Wake-up Lora module and re-configure it.
 * 
 * Return 1 on success, else 0.
 *****************************************************************************/
int DigiWake() {
    if(lora.begin(LORA_CS, LORA_RESET, LORA_DIO) == ERR_CHIP_NOT_FOUND) return 0;   
    lora.setFrequency((FREQ * 1000000.0)+FREQ_ERR);   // APRS freq
    lora.setPpmError(PPM_ERR);
    lora.setPower(LORA_POWER);  // dbm (max 20)
    delay(50);
    return 1;
}


/******************************************************************************
 * void DigiInit()
 *
 * Initialize digi radio module.
 *****************************************************************************/
int DigiInit() {

    Beacon1Timer = wdt_clk + (uint32_t)B1_INTERVAL;;
    Beacon2Timer = wdt_clk + (uint32_t)B2_INTERVAL;
    Beacon3Timer = wdt_clk + (uint32_t)B3_INTERVAL;
    TelemTimer   = wdt_clk + (uint32_t)TELEM_INTERVAL; 
	return DigiWake();
}
