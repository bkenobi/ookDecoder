/**********************************************************************
 * Arduino code to sniff the Acurite 00592TX wireless temperature
 * probe output data stream.
 *
 * Ideas on decoding protocol and prototype code from
 * Ray Wang (Rayshobby LLC) http://rayshobby.net/?p=8998
 *
 * Sniff the AcuRite model 00771W Indoor / Outdoor Thermometer
 * wireless data stream and display the results.
 * http://www.acurite.com/media/manuals/00754-instructions.pdf
 *
 * Code based on Ray Wang's humidity_display.ino source.
 * Heavily modified by Brad Hunting.
 *
 * The 00592TX wireless temperature probe contains a 433 MHz
 * wireless transmitter. The temperature from the probe is
 * sent approximately every 16 seconds.
 *
 * The 00592TX typically only sends one SYNC pulse + DATA stream
 * per temperature reading. Infrequently two sync/data streams
 * are sent during the same transmit window but that seems to 
 * be the exception.
 *
 * Ray Wang's code is for a different model of probe, one that 
 * transmits both temperature and humidity. Ray' code relies on 
 * two sync streams with a preceeding delay. 
 * 
 * The 00592TX usually starts the data sync bits right after
 * the RF sync pulses which are random length and polarity.
 *
 * Do not rely on a dead/mark time at the beginning of the 
 * data sync stream.
 *
 * The 00592TX first emits a seemingly random length string of 
 * random width hi/lo pulses, most like to provide radio
 * radio synchronization.
 *
 * The probe then emits 4 data sync pulses of approximately 50% 
 * duty cycle and 1.2 ms period. The sync pulses start with a 
 * high level and continue for 4 high / low pulses.
 *
 * The data bits immediately follow the fourth low of the data
 * sync pulses. Data bits are sent every ~0.6 msec as:
 *
 * 1 bit ~0.4 msec high followed by ~0.2 msec low
 * 0 bit ~0.2 msec high followed by ~0.4 msec low
 *
 * The 00592TX sends the 4 sync pulses followed by
 * 7 bytes of data equalling 56 bits.
 *
 * The code below works by receiving a level change interrupt 
 * on each changing edge of the data stream from the RF module
 * and recording the time in uSec between each edge.
 *
 * 8 measured hi and lo pulses in a row, 4 high and 4 low, of 
 * approximately 600 uSec each constitue a sync stream.
 *
 * The remaining 56 bits of data, or 112 edges, are measured
 * and converted to 1s and 0s by checking the high to low
 * pulse times.
 *
 * The first 4 pulses, or 8 edges, are the sync pulses followed
 * by the 56 bits, or 112 edges, of the data pulses.
 *
 * We measure 8 sync edges followed by 112 data edges so the 
 * time capture buffer needs to be at least 120 long.
 *
 * This code presently does not calculate the checksum of
 * the data stream. It simply displays the results of what was 
 * captured from the RF module.
 *
 * The data stream is 7 bytes long.
 * The first and second bytes are unique address bytes per probe.
 *   The upper two bits of the first byte are the probe channel indicator:
 *   11 = channel A
 *   10 = channel B
 *   00 = channel C
 *   The remaining 6 bits of the first byte and the 8 bits of the second
 *   byte are a unique identifier per probe.
 * The next two bytes seem to always be 0x44 followed by 0x90, for all of
 *   the probes I have tested (a sample of 6 probes).
 * The next two bytes are the temperature. The temperature is encoded as the
 *   lower 7 bits of both bytes with the most significant bit being an
 *   even parity bit.  The MSB will be set if required to insure an even
 *   number of bits are set to 1 in the byte. If the least significant
 *   seven bits have an even number of 1 bits set the MSB will be 0,
 *   otherwise the MSB will be set to 1 to insure an even number of bits.
 * The last byte is a simple running sum, modulo 256, of the previous 6 data bytes.
 */

#define SYNC            600
#define PULSE_LONG      400
#define PULSE_SHORT     200
#define BIT1_HIGH       PULSE_LONG
#define BIT1_LOW        PULSE_SHORT
#define BIT0_HIGH       PULSE_SHORT
#define BIT0_LOW        PULSE_LONG
#define PULSE_TOL       100

#define LED          13

// data is 7 bytes, 56 bits, or 112 edges
#define MAXBITS      112


class Acurite592TX : public DecodeOOK {
protected:
    byte i;
        
    byte datapulses=0;
    
    int tempA=-99;
    int tempB=-99;
    int tempC=-99;
    
    bool batteryAok;
    bool batteryBok;
    bool batteryCok;
    
    //set when high half of bit is checked
    // 0 : BIT0_HIGH received
    // 1 : BIT1_HIGH received
    // 2 : not set
    byte receivingBit = 2;
    
    char debug[100];
    
public:
    Acurite592TX () {}
    
    virtual char decode (word width) {
      if (PULSE_SHORT-PULSE_TOL <= width && width <= SYNC+PULSE_TOL) {
        switch (state) {
          case UNKNOWN:  //no data yet
            // sync is 4 high-low pulses (8 total) of 600 uS

            if (SYNC-PULSE_TOL<width && width<SYNC+PULSE_TOL) {
              flip++;
              state = OK;
            } else {
              return -1;
            }
            break;
            
          case OK:       //in preamble
            flip++;
            if (SYNC-PULSE_TOL<width && width<SYNC+PULSE_TOL) {
            } else if (flip>6) {  //could have missed preamble pulses, so check if this is data
              state = T0;
              flip = 9;
              datapulses++;
              
              //high/1st pulse
              if (receivingBit==2) {
                // data starts with high pulse
                if (BIT1_HIGH-PULSE_TOL<width && width<BIT1_HIGH+PULSE_TOL) {
                  receivingBit=1;
                } else if (BIT0_HIGH-PULSE_TOL<width && width<BIT0_HIGH+PULSE_TOL) {
                  receivingBit=0;
                } else {
                  return -1;  //data bit failed
                }
              } else {
                return -1;  //high/1st data bit failed
              }
            } else {
              return -1;  //preamble failed
            }
            break;
            
          case T0:  //data started
            flip++;
            datapulses++;
            if (receivingBit == 0 && BIT0_LOW-PULSE_TOL<width && width<BIT0_LOW+PULSE_TOL) {
              //0 bit low pulse. Bit received.
              gotBit(0);
              receivingBit=2;
            } else if (receivingBit == 1 && BIT1_LOW-PULSE_TOL<width && width<BIT1_LOW+PULSE_TOL) {
              //1 bit low pulse. Bit received.
              gotBit(1);
              receivingBit=2;
            } else if (receivingBit == 2 && BIT1_HIGH-PULSE_TOL<width && width<BIT1_HIGH+PULSE_TOL) {
              //1 bit high pulse
              receivingBit=1;
            } else if (receivingBit == 2 && BIT0_HIGH-PULSE_TOL<width && width<BIT0_HIGH+PULSE_TOL) {
              //0 bit high pulse
              receivingBit=0;
            } else {
              return -1;  //data bit failed
            }
            break;
          }
        } else {
          return -1;  //pulse length out of range
        }
        
        if (datapulses == MAXBITS) {
          return 1;
        }
        return 0;
    }
    
    bool nextPulse (word width) {
        if (state != DONE)
            switch (decode(width)) {
                case -1: 
                  resetDecoder(); 
                  break;
                  
                case 1:
                  done(); 
                  reverseBits();
                  break;
            }
        return isDone();
    }
    
    void resetDecoder (void) {
        i = datapulses = 0;
        receivingBit=2;
        DecodeOOK::resetDecoder();
    }

    

// Acurite 592TX temperature sensor decode functions derived from 
// Brad Hunting's Acurite_00592TX_sniffer project
// https://github.com/bhunting/Acurite_00592TX_sniffer
    
    void DecodePacket(void) {
      char packet[100];
      
      noInterrupts();
      if (checkData()) {
        //Serial.println("valid data");
//        Serial.println("592");
        int channel = getChannel(data[0]);
        if (channel == 1) {
          tempA = getTempF(data[4], data[5]);
        } else if (channel == 2) {
          tempB = getTempF(data[4], data[5]);
        } else if (channel == 3) {
          tempC = getTempF(data[4], data[5]);
        }
        
        //no knowledge of battery bit currently
        batteryAok=true;  
        batteryBok=true;  
        batteryCok=true;  
        
      
        Report(packet);
        Serial.print("Acurite 592TX: ");
        Serial.println(packet);
        
        //digitalWrite(LED, LOW);
      } else {
        //Serial.println("invalid data");
      }
      interrupts();
    }
    
    int getTempF(byte hibyte, byte lobyte) {
      // range -40 to 158 F
      int highbits = (hibyte & 0xF) << 7;
      int lowbits = lobyte & 0x7F;
      int rawtemp = highbits | lowbits;
      
      //new decoded temperature
      //return (int)((rawtemp-1000.)/10.);  //degC
      return (int)((rawtemp-1000.)/10.*9./5.+32.);  //degF
      
      //Brian Hunting's decoded temperature
      //return (int)((rawtemp - 1024) / 10.0+2.4+0.5);  //degC
      //return (int)((rawtemp - 1024) / 10.0+2.4+0.5)*9/5+32;  //degF
    }
    
    int getChannel(byte firstByte) {
/*
 *   Upper 2 bits define channel
 *   11 = channel A = return 1
 *   10 = channel B = return 2
 *   00 = channel C = return 3
 */
      int Channel = firstByte >> 6;
      if (Channel == 3) {
        return 1;
      } else if (Channel == 2) {
        return 2;
      } else if (Channel == 0) {
        return 3;
      }
    }
    
    int checkData(void) {
      int sum = 0;
      for (int i = 0; i < 6; i++) {
        sum += data[i];
      }
      sum -= data[6];
      if (sum%256 == 0) {
        return 1;
      } else {
        return 0;
      }
    }
    
    //Generate MQTT report and set temps to -99 so we don't report same data again
    void MQTTreport (char* packet) {
      sprintf(packet,"");
      
      if (tempA != -99) {
        sprintf(packet,"TempA=%d,BatteryA=%d", tempA, batteryAok);
        tempA = -99;
      }
      if (tempB != -99) {
        sprintf(packet,",TempB=%d,BatteryB=%d", tempB, batteryBok);
        tempB = -99;
      }
      if (tempC != -99) {
        sprintf(packet,",TempC=%d,BatteryC=%d", tempC, batteryCok);
        tempC = -99;
      }
    }
    
    //Generate internal debug report
    void Report (char* packet) {
      sprintf(packet,"");
      
      if (tempA != -99) {
        sprintf(packet,"TempA=%d,BatteryA=%d", tempA, batteryAok);
      }
      if (tempB != -99) {
        sprintf(packet,",TempB=%d,BatteryB=%d", tempB, batteryBok);
      }
      if (tempC != -99) {
        sprintf(packet,",TempC=%d,BatteryC=%d", tempC, batteryCok);
      }
    }
};
