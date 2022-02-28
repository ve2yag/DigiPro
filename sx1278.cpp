
#include "sx1278.h"

#include <SPI.h>

#define LORA_SCLK 2000000      // At 11.0592MHz, theoric max speed will be 2.7MHz (/4)
#define SPI_READ  0b00000000
#define SPI_WRITE 0b10000000
 
SX1278::SX1278(uint8_t bw, uint8_t sf, uint8_t cr) {

    /* DEFAULT FREQ */
    _frequency = 433300000;
    
    /* DEFAULT POWER IN dbm */
    _power = 17;        

    /* MODEM SETTING */
    _bw = bw;
    _sf = sf;
    _cr = cr;
    _mode = SX1278_STANDBY;
}

uint8_t SX1278 :: getRegValue(uint8_t reg, uint8_t msb, uint8_t lsb) {
    if((msb > 7) || (lsb > 7) || (lsb > msb)) return(ERR_INVALID_BIT_RANGE);
    uint8_t rawValue = readRegister(reg);
    uint8_t maskedValue = rawValue & ((0b11111111 << lsb) & (0b11111111 >> (7 - msb)));
    return(maskedValue);
}

uint8_t SX1278 :: setRegValue(uint8_t reg, uint8_t value, uint8_t msb, uint8_t lsb) {
    if((msb > 7) || (lsb > 7) || (lsb > msb)) return(ERR_INVALID_BIT_RANGE);
    uint8_t currentValue = readRegister(reg);
    uint8_t newValue = currentValue & ((0b11111111 << (msb + 1)) & (0b11111111 >> (8 - lsb)));
    writeRegister(reg, newValue | value);
    return(ERR_NONE);
}

uint8_t SX1278 :: readRegister(uint8_t reg) {
  SPI.beginTransaction(SPISettings(LORA_SCLK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    SPI.transfer(reg | SPI_READ);
    uint8_t result = SPI.transfer(0xFF);
    digitalWrite(_cs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
    return result; 
}

uint8_t SX1278 :: readRegisterBurst(uint8_t reg, uint8_t numBytes, uint8_t *inBytes) {
    uint8_t outBytes[256];
    
  SPI.beginTransaction(SPISettings(LORA_SCLK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    SPI.transfer(reg | SPI_READ);
    for(uint8_t i=0; i<numBytes; i++) inBytes[i] = SPI.transfer(0xFF);
    digitalWrite(_cs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
    return(ERR_NONE);
}

void SX1278 :: writeRegister(uint8_t reg, uint8_t data) {
  SPI.beginTransaction(SPISettings(LORA_SCLK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    SPI.transfer(reg | SPI_WRITE);
    SPI.transfer(data);
    digitalWrite(_cs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
}

void SX1278::writeRegisterBurst(uint8_t reg, uint8_t *data, uint8_t numBytes) {
  SPI.beginTransaction(SPISettings(LORA_SCLK, MSBFIRST, SPI_MODE0));
    digitalWrite(_cs, LOW);
    SPI.transfer(reg | SPI_WRITE);
    for(uint8_t i=0; i<numBytes; i++) SPI.transfer(data[i]);
    digitalWrite(_cs, HIGH);
    SPI.transfer(0xFF);
    SPI.endTransaction();
}

uint8_t SX1278::begin(int8_t cs, int8_t rst, int8_t dio0) {
  _cs = cs;
  _reset = rst;
  _dio0 = dio0;
  
  /* INIT SPI PORT AND I/O PORT */
    SPI.begin();
    pinMode(_cs, OUTPUT);        
    if(_dio0 != -1) pinMode(_dio0, INPUT);
    digitalWrite(_cs, HIGH);

    /* HARDWARE RESET MODULE */
    if(_reset != -1) {
        pinMode(_reset, OUTPUT);
        digitalWrite(_reset, LOW);
        delay(35);
        digitalWrite(_reset, HIGH);
        delay(35);
    }
    
    /* TRY TO DETECT MODULE */
    uint8_t i = 0;
    bool flagFound = false;
    while((i < 10) && !flagFound) {
        uint8_t version = readRegister(SX1278_REG_VERSION);
        if(version == 0x12) {
            flagFound = true;
        } else {
            delay(10);
            i++;
        }
    }
  
    if(!flagFound) {
        return(ERR_CHIP_NOT_FOUND);
    }
  
    return(config(_bw, _sf, _cr));
}

void SX1278::end(void) {
    setMode(SX1278_SLEEP);
}

uint8_t SX1278::tx(uint8_t*data, uint8_t length) {
    setMode(SX1278_STANDBY);

    setRegValue(SX1278_REG_DIO_MAPPING_1, SX1278_DIO0_TX_DONE, 7, 6);
    clearIRQFlags();
  
    writeRegister(SX1278_REG_PAYLOAD_LENGTH, length);
    writeRegister(SX1278_REG_FIFO_TX_BASE_ADDR, SX1278_FIFO_TX_BASE_ADDR_MAX);
    writeRegister(SX1278_REG_FIFO_ADDR_PTR, SX1278_FIFO_TX_BASE_ADDR_MAX);  
    writeRegisterBurst(SX1278_REG_FIFO, data, length);
    setMode(SX1278_TX);
    delay(2);       // Set DIO0 rise to HIGH before check txBusy

    return(ERR_NONE);
}

uint8_t SX1278::txBusy() {
  if(_dio0 != -1) {
    if(digitalRead(_dio0) == HIGH) {
      clearIRQFlags();
      return 0;   
    }
  } else {
    if(readRegister(SX1278_REG_IRQ_FLAGS) & SX1278_CLEAR_IRQ_FLAG_TX_DONE) {
      clearIRQFlags();
      return 0;   
    }
  }
 
    return 1;
}

uint8_t SX1278::rxBusy(void) {

    /* CHECK IF RX ARE IN RX_CONTINUOUS MODE */
    if(_mode != SX1278_RXCONTINUOUS) InitReceiver(); 

    /* CHECK SIGNAL DETECT BIT */
    if(readRegister(SX1278_REG_MODEM_STAT) & SX1278_STATUS_SIG_DETECT) return 1;
    return 0;
}

void SX1278::InitReceiver() {
    setMode(SX1278_STANDBY);
    setRegValue(SX1278_REG_DIO_MAPPING_1, SX1278_DIO0_RX_DONE | SX1278_DIO1_RX_TIMEOUT, 7, 4);
    clearIRQFlags();  
    writeRegister(SX1278_REG_FIFO_RX_BASE_ADDR, SX1278_FIFO_RX_BASE_ADDR_MAX);
    writeRegister(SX1278_REG_FIFO_ADDR_PTR, SX1278_FIFO_RX_BASE_ADDR_MAX);
    writeRegister(SX1278_REG_RX_NB_BYTES, 0);
    if(_sf == SX1278_SF_6) writeRegister(SX1278_REG_PAYLOAD_LENGTH, _sf6length); // Set fixed length packet when SF6 is selected
    setMode(SX1278_RXCONTINUOUS);
    delay(1);  
}

uint8_t SX1278::rxAvailable(uint8_t *data, uint8_t *length) {
    int status;

    /* SET PACKET LENGTH IN CASE OF SF6 */
    _sf6length = *length;
    
    /* CHECK IF RX ARE IN RX_CONTINUOUS MODE */
    if(_mode != SX1278_RXCONTINUOUS) InitReceiver(); 

    /* CHECK IF PACKET AVAILABLE */
    *length = 0;
  if(_dio0 != -1) {
    if(digitalRead(_dio0) == LOW) return(ERR_RX_EMPTY);
  } else {
    if(!(readRegister(SX1278_REG_IRQ_FLAGS) & SX1278_CLEAR_IRQ_FLAG_RX_DONE)) return(ERR_RX_EMPTY);
  }
    setMode(SX1278_STANDBY);

    /* CHECK PAYLOAD CRC */
    if(readRegister(SX1278_REG_IRQ_FLAGS) & SX1278_CLEAR_IRQ_FLAG_PAYLOAD_CRC_ERROR) {
        clearIRQFlags();
        return(ERR_CRC_MISMATCH);  
    }
    
    /* READ PACKET */
    uint8_t headerMode = readRegister(SX1278_REG_MODEM_CONFIG_1) & SX1278_HEADER_IMPL_MODE;
    if(headerMode == SX1278_HEADER_EXPL_MODE) *length = readRegister(SX1278_REG_RX_NB_BYTES);
    readRegisterBurst(SX1278_REG_FIFO, *length, data);
    clearIRQFlags();
    return(ERR_NONE);
}

uint8_t SX1278::setMode(uint8_t mode) {
    _mode = mode;
    setRegValue(SX1278_REG_OP_MODE, mode, 2, 0);
    return(ERR_NONE);
}

uint8_t SX1278::getMode() {   
    _mode = getRegValue(SX1278_REG_OP_MODE, 2, 0);
    return _mode;
}

void SX1278::setFrequency(uint32_t frequency) {
    _frequency = frequency;
    uint64_t frf = ((uint64_t)_frequency * 524288L) / 32000000L;
 
    writeRegister(SX1278_REG_FRF_MSB, (uint8_t)(frf >> 16));
    writeRegister(SX1278_REG_FRF_MID, (uint8_t)(frf >> 8));
    writeRegister(SX1278_REG_FRF_LSB, (uint8_t)(frf >> 0));
}


void SX1278::setOCP(uint8_t mA) {
    uint8_t ocpTrim = 27;

    if (mA <= 120) {
        ocpTrim = (mA - 45) / 5;
    } else if (mA <=240) {
      ocpTrim = (mA + 30) / 10;
    }

    writeRegister(SX1278_REG_OCP, SX1278_OCP_ON | (0x1F & ocpTrim));
}


void SX1278::setPower(uint8_t level) {

    _power = level;
    if (level > 17) {
        if (level > 20) level = 20;

        // subtract 3 from level, so 18 - 20 maps to 15 - 17
        level -= 3;

        // High Power +20 dBm Operation (Semtech SX1276/77/78/79 5.4.3.)
        writeRegister(SX1278_REG_PA_DAC, 0x87);
        setOCP(140);
        
    } else {
        if (level < 2) level = 2;
      
        //Default value PA_HF/LF or +17dBm
        writeRegister(SX1278_REG_PA_DAC, 0x84);
        setOCP(100);
    }

    // Output power map 0-15
    level -= 2; 
    writeRegister(SX1278_REG_PA_CONFIG, SX1278_PA_SELECT_BOOST | level);
}

uint8_t SX1278::config(uint8_t bw, uint8_t sf, uint8_t cr) {
    uint8_t status = ERR_NONE;

    // Refresh private variable
    _bw = bw;
    _sf = sf;
    _cr = cr;
    
    // set mode to SLEEP
    status = setMode(SX1278_SLEEP);
    if(status != ERR_NONE) return(status);
  
    // set LoRa mode
    status = setRegValue(SX1278_REG_OP_MODE, SX1278_LORA, 7, 7);
    if(status != ERR_NONE) return(status);
  
    // set carrier frequency
    setFrequency(_frequency);
  
    // output power configuration
    setPower(_power);
    
    // set lna gain
    status = setRegValue(SX1278_REG_LNA, SX1278_LNA_GAIN_1);
    if(status != ERR_NONE) return(status);

  // Set AGC and low bit rate optimizer
  unsigned char ldro = SX1278_LOW_DATA_RATE_OPT_OFF;
  if(bw == SX1278_BW_7_80_KHZ && sf > SX1278_SF_6) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_10_40_KHZ && sf > SX1278_SF_7) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_15_60_KHZ && sf > SX1278_SF_7) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_20_80_KHZ && sf > SX1278_SF_8) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_31_25_KHZ && sf > SX1278_SF_8) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_41_70_KHZ && sf > SX1278_SF_9) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_62_50_KHZ && sf > SX1278_SF_9) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_125_00_KHZ && sf > SX1278_SF_10) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
  if(bw == SX1278_BW_250_00_KHZ && sf > SX1278_SF_11) ldro = SX1278_LOW_DATA_RATE_OPT_ON;
    status = setRegValue(SX1278_REG_MODEM_CONFIG_3, SX1278_AGC_AUTO_ON | ldro);
    if(status != ERR_NONE) return(status);
  
    // turn off frequency hopping
    status = setRegValue(SX1278_REG_HOP_PERIOD, SX1278_HOP_PERIOD_OFF);
    if(status != ERR_NONE) return(status);
  
    // basic setting (bw, cr, sf, header mode and CRC)
    if(sf == SX1278_SF_6) {
        status = setRegValue(SX1278_REG_MODEM_CONFIG_2, SX1278_SF_6 | SX1278_TX_MODE_SINGLE | SX1278_RX_CRC_MODE_ON, 7, 2);
        status = setRegValue(SX1278_REG_MODEM_CONFIG_1, bw | cr | SX1278_HEADER_IMPL_MODE);
        status = setRegValue(SX1278_REG_DETECT_OPTIMIZE, SX1278_DETECT_OPTIMIZE_SF_6, 2, 0);
        status = setRegValue(SX1278_REG_DETECTION_THRESHOLD, SX1278_DETECTION_THRESHOLD_SF_6);
    } else {
        status = setRegValue(SX1278_REG_MODEM_CONFIG_2, sf | SX1278_TX_MODE_SINGLE | SX1278_RX_CRC_MODE_ON, 7, 2);
        status = setRegValue(SX1278_REG_MODEM_CONFIG_1, bw | cr | SX1278_HEADER_EXPL_MODE);
        status = setRegValue(SX1278_REG_DETECT_OPTIMIZE, SX1278_DETECT_OPTIMIZE_SF_7_12, 2, 0);
        status = setRegValue(SX1278_REG_DETECTION_THRESHOLD, SX1278_DETECTION_THRESHOLD_SF_7_12);
    }
  
    if(status != ERR_NONE) return(status);
  
    // set default preamble length
    status = setRegValue(SX1278_REG_PREAMBLE_MSB, SX1278_PREAMBLE_LENGTH_MSB);
    status = setRegValue(SX1278_REG_PREAMBLE_LSB, SX1278_PREAMBLE_LENGTH_LSB);
    if(status != ERR_NONE) return(status);
  
    // set mode to STANDBY
    status = setMode(SX1278_STANDBY);
    if(status != ERR_NONE) return(status);
  
    return(ERR_NONE);
}

int16_t SX1278::getLastPacketRSSI(void) {
    return(-164 + (uint16_t)getRegValue(SX1278_REG_PKT_RSSI_VALUE));
}

void SX1278::setPpmError(char err) {
    writeRegister(SX1278_REG_PPMCORRECTION, (uint8_t)err);
}

void SX1278::clearIRQFlags(void) {
    writeRegister(SX1278_REG_IRQ_FLAGS, 0b11111111);
}

void SX1278::setSyncword(uint8_t sync) {
    writeRegister(SX1278_REG_SYNC_WORD, sync);
}
