/* Code derived from Jens Jensen "acurite5n1arduino"
* 
*/

// pulse timings
// SYNC
#define SYNC_HI      725
#define SYNC_LO      575

// LONG == 1
#define LONG_HI      525
#define LONG_LO      375

// SHORT == 0
#define SHORT_HI     325
#define SHORT_LO     175

// other settables
#define LED          13
#define MAXBITS      64  // max framesize


class Acurite5n1 : public DecodeOOK {
protected:
    byte i;
    
    // wind directions:
    // { "NW", "WSW", "WNW", "W", "NNW", "SW", "N", "SSW",
    //   "ENE", "SE", "E", "ESE", "NE", "SSE", "NNE", "S" };
    const float winddirections[16] = { 315.0, 247.5, 292.5, 270.0, 
                                     337.5, 225.0, 0.0, 202.5,
                                     67.5, 135.0, 90.0, 112.5,
                                     45.0, 157.5, 22.5, 180.0 };

    // message types
    #define  MT_WS_WD_RF  49    // wind speed, wind direction, rainfall
    #define  MT_WS_T_RH   56    // wind speed, temp, RH
    
    byte datapulses=0;
    
    unsigned int   raincounter = 0;
    float rainfall;
    unsigned int curraincounter;
    float windspeedkph = -99;
    float winddir;
    float tempf;
    int humidity;
    bool batteryok;
    
    //print related
    uint32_t g_PrintTime_ms = 0;
    uint32_t g_PrevPrintTime_ms = 0;
    uint32_t g_PrintTimeDelta_ms = 0;
          
public:
    Acurite5n1 () {}
    
    virtual char decode (word width) {
      if (SHORT_LO <= width && width <= SYNC_HI) {
          switch (state) {
                case UNKNOWN:  //no data yet
                    if (SYNC_LO<width && width<SYNC_HI) {
                        //valid start pulse is short high
                        flip++;
                        state = OK;
                    }
                    else {
                      return -1;
                    }
                    break;
                    
                case OK:       //in preamble
                    flip++;
                    if (SYNC_LO<width && width<SYNC_HI) {
                    }
                    else if (flip>3) {  //could have missed preamble pulses, so check if this is data
                      state = T0;
                      flip = 9;
                      datapulses++;
                      if (LONG_LO<width && width<LONG_HI) {
                        gotBit(1);
                      } 
                      else if (SHORT_LO<width && width<SHORT_HI) {
                        gotBit(0);
                      }
                      else {
                        return -1;  //preamble failed
                      }
                    }
                    else {
                        return -1;  //preamble failed
                    }
                    break;
                    
                case T0:  //data started
                    flip++;
                    if (flip%2 == 1) { //odd pulse is high
                      datapulses++;
                      if (LONG_LO<width && width<LONG_HI) {
                        gotBit(1);
                      } 
                      else if (SHORT_LO<width && width<SHORT_HI) {
                        gotBit(0);
                      } 
                      else {
                        return -1;
                      }
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
        i = 0;
        datapulses=0;
        DecodeOOK::resetDecoder();
    }
    

// Acurite 5n1 decode functions shamelessly stolen from Jens Jensen's project
// https://github.com/zerog2k/acurite5n1arduino

    void DecodePacket() {
      char packet[100];
      
      noInterrupts();  //should interrupts be turned off for decoding?
      if (acurite_crc(data, pos)) {
        // passes crc, good message
        digitalWrite(LED, HIGH);   
        
//        Serial.println("5n1");  

        windspeedkph = getWindSpeed(data[3], data[4]);
        
        int msgtype = (data[2] & 0x3F);
        if (msgtype == MT_WS_WD_RF) {
          // wind speed, wind direction, rainfall
          rainfall = 0.00;
          curraincounter = getRainfallCounter(data[5], data[6]);
          
          if (raincounter > 0) {
            // track rainfall difference after first run
            rainfall = (curraincounter - raincounter) * 0.01;
          } else {
            // capture starting counter
            raincounter = curraincounter; 
          }
          
          winddir = getWindDirection(data[4]);
          
        } else if (msgtype == MT_WS_T_RH) {
          // wind speed, temp, RH
          tempf = getTempF(data[4], data[5]);
          humidity = getHumidity(data[6]);
          batteryok = ((data[2] & 0x40) >> 6);
        }
        
        Report(packet);
        Serial.print("Acurite 5n1: ");
        Serial.println(packet);
      }
      
      digitalWrite(LED, LOW);
      interrupts();  //should interrupts be turned off for decoding?
    }
    
    bool acurite_crc(volatile byte row[], int cols) {
    	// sum of first n-1 bytes modulo 256 should equal nth byte
    	cols -= 1; // last byte is CRC
        int sum = 0;
    	for (int i = 0; i < cols; i++) {
    	  sum += row[i];
    	}    
    	if (sum != 0 && sum % 256 == row[cols]) {
    	  return true;
    	} else {
    	  return false;
    	}
    }
     
    float getTempF(byte hibyte, byte lobyte) {
      // range -40 to 158 F
      int highbits = (hibyte & 0x0F) << 7;
      int lowbits = lobyte & 0x7F;
      int rawtemp = highbits | lowbits;
      float temp = (rawtemp - 400) / 10.0;
      return temp;
    }

    float getWindSpeed(byte hibyte, byte lobyte) {
      // range: 0 to 159 kph
      int highbits = (hibyte & 0x7F) << 3;
      int lowbits = (lobyte & 0x7F) >> 4;
      float speed = highbits | lowbits;
      // speed in m/s formula according to empirical data
      if (speed > 0) {
        speed = speed * 0.23 + 0.28;
      }
      float kph = speed * 60 * 60 / 1000;
      return kph;
    }

    float getWindDirection(byte b) {
      // 16 compass points, ccw from (NNW) to 15 (N), 
            // { "NW", "WSW", "WNW", "W", "NNW", "SW", "N", "SSW",
            //   "ENE", "SE", "E", "ESE", "NE", "SSE", "NNE", "S" };
      int direction = b & 0x0F;
      return winddirections[direction];
    }

    int getHumidity(byte b) {
      // range: 1 to 99 %RH
      int humidity = b & 0x7F;
      return humidity;
    }

    int getRainfallCounter(byte hibyte, byte lobyte) {
      // range: 0 to 99.99 in, 0.01 increment rolling counter
      int raincounter = ((hibyte & 0x7f) << 7) | (lobyte & 0x7F);
      return raincounter;
    }

    float convKphMph(float kph) {
      return kph * 0.62137;
    }

    float convFC(float f) {
      return (f-32) / 1.8;
    }

    float convInMm(float in) {
      return in * 25.4;
    }

    //Generate MQTT report and set wind speed to -99 so we don't report same data again
    void MQTTreport (char* packet) {
      float windspeed = convKphMph(windspeedkph);
      char str_temp[10];
      char str_winds[10];
      char str_windd[10];
      char str_rain[10];
      
      sprintf(packet,"");
      
      if (windspeedkph != -99) {
        dtostrf(tempf,5,1,str_temp);
        dtostrf(windspeed,5,1,str_winds);
        dtostrf(winddir,4,1,str_windd);
        dtostrf(rainfall,5,2,str_rain);
        
        sprintf(packet,"Windspeed=%s,Winddir=%s,Rainfall=%s,TempF=%s,Humidity=%u,Battery=%u",
          str_winds, str_windd, str_rain, str_temp, humidity, batteryok);
        
        windspeedkph = -99;
      }
    }

    //Generate internal debugging report
    void Report (char* packet) {
      float windspeed = convKphMph(windspeedkph);
      char str_temp[10];
      char str_winds[10];
      char str_windd[10];
      char str_rain[10];
      
      sprintf(packet,"");
      
      if (windspeedkph != -99) {
        dtostrf(tempf,5,1,str_temp);
        dtostrf(windspeed,5,1,str_winds);
        dtostrf(winddir,4,1,str_windd);
        dtostrf(rainfall,5,2,str_rain);
        
        sprintf(packet,"Windspeed=%s,Winddir=%s,Rainfall=%s,TempF=%s,Humidity=%u,Battery=%u",
          str_winds, str_windd, str_rain, str_temp, humidity, batteryok);
      }
    }
};
