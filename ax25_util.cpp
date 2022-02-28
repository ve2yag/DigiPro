
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/*-------------------------------------------------------------------
 *
 * Name:        AXCall2asc 
 *
 * Purpose:     Convert AX25 call in plain ASCII
 *
 * Inputs:    
 *        
 * Note:    
 *              
 *-----------------------------------------------------------------*/
char *AXCall2asc(unsigned char *buf) {    
  static char call[16];

  /* GET CALLSIGN, SKIP SPACE */
  memset(call, 0, 16);
  for (int j=0; j<6; j++) 
    if (buf[j] != (' '<<1)) call[j] = (buf[j]>>1);  // Not a space, store it      

  /* WRITE SSID AT THE END */
  uint8_t ssid = (buf[6]>>1) & 0x0F;
  if(ssid) sprintf(strrchr(call, 0), "-%u", ssid); 
  
  return call;
}


/*-------------------------------------------------------------------
 *
 * Name:        DecodeAX25 
 *
 * Purpose:     Convert packet to readable ascii
 *
 * Inputs:    
 *        
 * Note:    
 *              
 *-----------------------------------------------------------------*/
void DecodeAX25(uint8_t *data, uint8_t length, char *out) {
    strcpy(out, AXCall2asc(&data[7]));  // SOURCE CALL
    strcat(out, ">");
    strcat(out, AXCall2asc(&data[0]));  // DEST CALL
    int i = 7;
    while((data[i+6] & 1) == 0) {     // DIGI PATH
		i+=7;
		strcat(out, ",");
		strcat(out, AXCall2asc(&data[i]));
		if(data[i+6] & 128) strcat(out, "*");
    } 
    
    i+=7+2;               // JUMP PID/DATA TYPE AND DISPLAY APRS PACKET
    strcat(out, ":");
    strncat(out, (char*)&data[i], length-i);    // PACKET
}


/*-------------------------------------------------------------------
 *
 * Name:        asc2AXCall
 *
 * Purpose:     Convert ASCII call to AX25 callsign
 *
 * Inputs:    
 *        
 * Note:    
 *              
 *-----------------------------------------------------------------*/
void asc2AXcall(char *in, unsigned char *out) {

  /* GET CALLSIGN, INSERT SPACE */
  for(int i=0; i<6; i++) {
    if(isalnum(*in)) {
      *(out++) = *(in++)<<1;
    } else {
      *(out++) = ' '<<1;
    }
  }
  
  /* SET SSID */
  if(*in == '-') {
    *out = 0x60 | ((atoi(++in)&15) <<1);
  } else {
    *out = 0x60;
  }
  
  /* SEARCH FOR DIGI * MARKER */
  char* p = strpbrk(in, ",:*");
  if(p!=0 && *p == '*') *out |= 0x80;   // Hass-been-digipeated bit
}


/*-------------------------------------------------------------------
 *
 * Name:        EncodeAX25 
 *
 * Purpose:     Convert ascii to AX25 packet
 *
 * Inputs:    
 *        
 * Note:    
 *              
 *-----------------------------------------------------------------*/
int EncodeAX25(char *in, uint8_t *out) {
  char *p;
  int pos;
  
  /* CONVERT SOURCE CALL */
  asc2AXcall(in, out+7);

  /* CONVERT DEST ALL, AFTER THE > CARACTER */
  p = strchr(in, '>');
  if(p==NULL) return 0;
  asc2AXcall(++p, out);
  
  /* CONVERT PATH, SEPARATE WITH , UNTIL END OF PATH WITH : */    
  pos = 14; // Position of path in AX25 packet (pos-1 to set final bit)
  p = in;
  while(1) {
    p = strpbrk(p, ",:");
    if(p==NULL) return 0;
    if(*p == ':') break;
    p++;
    asc2AXcall(p, out+pos);
    pos+=7;
  }
  p++;  // Skip : caracter
  
  /* SET AX25 PATH FINAL BIT */
  out[pos-1] |= 0x01;
  
  /* SET UI PACKET AND PID */
  out[pos++] = 0x03;
  out[pos++] = 0xF0;
  
  /* COPY PACKET DATA */
  while(*p != 0) out[pos++] = *(p++);
  
  return pos;
}
