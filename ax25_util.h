
void asc2AXcall(char *in, unsigned char *out);
char *AXCall2asc(unsigned char *buf);

int EncodeAX25(char *in, uint8_t *out);
void DecodeAX25(uint8_t *data, uint8_t length, char *out);
