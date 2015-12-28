/*
* Blue Line Innovations Power Meter Sensor
* Protocol defined by radredgreen/rtl_433
* documentation also at 
* http://scruss.com/blog/2013/12/03/blueline-black-decker-power-monitor-rf-packets/
*
* Data obtained by an IR-reader/sensor is transmitted in form of short bursts every 28.5 to 31.5
* seconds on 433.92 MHz (single frequency) . The carier is On/Off pulse modulated (logic ‘1’: O.5ms
* TX-on followed by 2ms TX-off. logic ‘O’: O.5ms TX- on followed by 4ms TX-off)."
*
* Lets start with some nomenclature on the bit sequence. Every 30 seconds there are 33 * 3 bursts of rf.
* Lets call these 33 * 3 bursts a 'packet'. Each packet contains 3 'frames'. Each frame contains
* 4 bytes. The first byte is always a leading preamble of 0xfe. The second and third bytes are the
* data of the frame and the fourth byte is the CRC over the second and third bytes (sometimes offset,
* see below).
*
* When the unit is first powered up it transmits a 16 bit 'Transmitter ID' with 0xfe preamble and CRC.
* Pressing the button on the transmitter causes it to retransmit this ID. Holding the button for 10
* seconds causes it to change it's ID.
*
* The two data bytes in a frame between the 0xfe preamble and the CRC are 'offset' by the transmitter id.
* The LSB comes over the air first and the byte order needs to be swapped before offset so that the carry
* between the LSB and the MSB is setup correctly.
*
* The CRC used is CRC-8-ATM with polynomial 100000111. This is calculated across the data bytes
* before the offset by transmitter ID except in a transmitter ID packet. This ensures different
* monitors can coexist.
*
* The first 2 frames are always equal to each other and may be the same or different than the 3rd
* frame. There are 3 types of packets that I've identified in addition to the transmitter id packet.
*
* The first is a 'power' packet. This packet can be identified by the least significant 2 bits
* of the 1st data bytes in frame 1 and 2 are '01'. The second data byte contains the MSB and
* the first data byte contains the LSB (including the least sig 2 bits - not sure about this). The
* 3rd frame is of the same format as the first 2 frames but can contain different data! Maybe the meter
* gets new data between the first 2 and last frame. In this case the hand held display uses the one of the
* first 2 frames. To convert from this 'count' to kilowatts, take 3600/count * your meter's Kh
* value (7.2 on my meter). This packet is repeated 4 times at approx 30 second intervals.
*
* The second is the 'temperature' packet. This packet can be identified by the least
* significant 2 bits of the 1st data bytes in frame 1 and 2 are '10'. The second data byte
* contains the temp data. I'm guessing this is 2's complement, but haven't gone through enough
* temp range to verify. The first byte contains unknown flags. I know low battery is in here but not sure
* where yet. The 3rd frame is a power frame with decoding the same as in the power packet. To decode
* temp take 0.75*temp byte - 19 to get to Fahrenheit or similar for Celsius. This packet comes 5th
* after 4 power packets.
*
* The third type of packet is the 'energy' packet. This packet can be identified by the least
* significant 2 bits of the 1st data bytes in frame 1 and 2 are '11'. The second data byte contains
* the MSB and the first data byte contains the LSB (excluding the least sig 2 bits - 14 bits total ).
* The 3rd frame is a power frame with decoding the same as in the power packet. To decode energy
* take 0.004 * energy value * your meter's Kh value (7.2 on my meter) to get to kWh. This packet
* comes 6th after the temperature packet. Then the packet cycle restarts with power packets.
*
*/

//#include "stdint.h"
#include "temp_lerp.h"

#define OOK_PACKET_INSTANT 1
#define OOK_PACKET_TEMP    2
#define OOK_PACKET_TOTAL   3

//Transmitter ID set on Blueline meter
#define DEFAULT_TX_ID 0x16E0

//Kh value of meter.  Typically 1 for digital and 7.2 for analog.
//Calculated 29.2
//Meter indicates 40
#define Kh 1.0


class Blueline : public DecodeOOK {
protected:
    byte i;
    bool g_battStatus;
    uint8_t g_RxTemperature;
    uint8_t g_RxFlags;
    uint16_t g_RxWatts = -99;
    uint16_t g_RxWattHours;
    uint16_t g_TxId = DEFAULT_TX_ID;  //This should work, but if the object gets reset for some reason it will potentially break 
    bool g_RxDirty;
    uint32_t g_RxLast;
    uint8_t packetTime;
    
    //print related
    uint32_t g_PrintTime_ms = 0;
    uint32_t g_PrevPrintTime_ms = 0;
    uint32_t g_PrintTimeDelta_ms = 0;
    
public:
    Blueline () {}
    
    String debug;
    
    virtual char decode (word width) {
      debug = String(String(++i,DEC) + "/" + String(width,DEC));
      
      if (375 <= width && width <= 1625) {
          switch (state) {
                case UNKNOWN:  //no data yet
                    if (width<750) {
                        //valid start pulse is short high
                        flip++;
                        state = OK;
                        packetTime = millis();
                    }
                    else {
                      return -1;
                    }
                    break;
                case OK:       //in preamble
                    if (width < 750) {
                        flip++;
                    }
                    else if (++flip >= 8 && width > 1250) {
                        //preamble is 7 short + 1 extra long low pulses
                        state = T0;  //preamble done
                        flip=16;  //flip should be 14 when 1500us pulse is seen
                    }
                    else {
                        return -1;  //preamble failed
                    }
                    break;
                case T0:  //data started
                    flip++;
                    if (flip%2 == 0) { //even pulse is low
                      gotBit(width < 750);
                    }
                    else
                    break;
            }
        } else {
          return -1;  //pulse length out of range
        }
        
        if (flip == 64) {
          return 1;
        }
        return 0;
    }
    
    bool nextPulse (word width) {
        if (state != DONE)
            switch (decode(width)) {
                case -1: resetDecoder(); break;
                case 1:  done(); reverseBits(); break;
            }
        return isDone();
    }
    
    void resetDecoder (void) {
        i = 0;
        g_RxDirty = false;
        DecodeOOK::resetDecoder();
    }
    
    //Receiving RX data
    bool IsDirty (void) {
      return g_RxDirty;
    }

    //Time last packet bit 1 was seen    
    uint8_t RxLast (void) {
      return g_RxLast;
    }

    //Generate MQTT report and reset g_RxWatts so we don't print same data multiple times
    void MQTTreport (char* packet) {
      int batt=0;
      
      sprintf(packet,"");
      
      if (g_RxWatts != -99) {
        if (g_battStatus) batt=1;
        sprintf(packet, "TotalEnergy=%u,CurrentPower=%u,TempF=%u,Battery=%u",
          g_RxWattHours, g_RxWatts, g_RxTemperature, batt);
        
        g_RxWatts = -99;
      }
    }

    //Generate report for debugging
    void Report (char* packet) {
      int batt=0;
      
      sprintf(packet,"");
      
      if (g_RxWatts != -99) {
        if (g_battStatus) batt=1;
        sprintf(packet, "TotalEnergy=%u,CurrentPower=%u,TempF=%u,Battery=%u",
          g_RxWattHours, g_RxWatts, g_RxTemperature, batt);
      }
    }
    
    
    
//Decode functions shamelessly stolen from https://github.com/CapnBry/Powermon433

    //crc8 from chromimum project
    __attribute__((noinline)) uint8_t crc8(uint8_t const *data, uint8_t len)
    {
      uint16_t crc = 0;
      for (uint8_t j=0; j<len; ++j)
      {
        crc ^= (data[j] << 8);
        for (uint8_t i=8; i>0; --i)
        {
          if (crc & 0x8000)
            crc ^= (0x1070 << 3);
          crc <<= 1;
        }
      }
      return crc >> 8;
    }
    
    void decodePowermon(uint16_t val16)
    {
      char packet[100];

//      Serial.println("blueline");
      switch (data[0] & 3)
      {
      case OOK_PACKET_INSTANT:
        // val16 is the number of milliseconds between blinks
        // Each blink is one watt hour consumed
        g_RxWatts = 3600000UL / val16 * Kh;
        break;
    
      case OOK_PACKET_TEMP:
        g_RxTemperature = temp_lerp(data[1]);
        g_RxFlags = data[0];
        g_battStatus = BatteryStatus(g_RxFlags);
        break;
    
      case OOK_PACKET_TOTAL:
        //g_PrevRxWattHours = g_RxWattHours;
        g_RxWattHours = 0.004 * val16 * Kh;
        // prevent rollover through the power of unsigned arithmetic
        //g_TotalRxWattHours += (g_RxWattHours - g_PrevRxWattHours);
        break;
      }
      
      Report(packet);
      Serial.print("Blueline: ");
      Serial.println(packet);
    }
    
    bool BatteryStatus(uint8_t data) {
      //https://github.com/radredgreen/rtl_433/blob/master/src/rtl_433.c#L498
      uint8_t battBit = ((data & 0xfc) >> 2 & 0x20) >> 5;
      return battBit==0;
    }
    
    void decodeRxPacket(void)
    {
      uint16_t val16 = *(uint16_t *)data;
      if (crc8(data, 3) == 0)
      {
        g_TxId = data[1] << 8 | data[0];
        Serial.print(F("NEW DEVICE id="));
        Serial.println(val16, HEX);
        return;
      }
    
      val16 -= g_TxId;
      data[0] = val16 & 0xff;
      data[1] = val16 >> 8;
      if (crc8(data, 3) == 0)
      {
        decodePowermon(val16 & 0xfffc);
        g_RxDirty = true;
        g_RxLast = millis();
      }
    }
};
