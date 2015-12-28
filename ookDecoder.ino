#define VERSION "v0.9 20151228"

#include <util/atomic.h>

#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>


#include "DecodeOOK.h"
#include "Blueline.h"
#include "Acurite5n1.h"
#include "Acurite592TX.h"

#define DPIN_OOK_RX  2
#define DPIN_LED     13
#define ARRAY_SIZE   200
#define REPORT_TIME  30000

byte mac[]    = {  0xDE, 0xED, 0xBA, 0xFE, 0xFE, 0xED };
byte server[] = { 192, 168, 0, 200 };
byte ip[]     = { 192, 168, 0,  70};

void callback(char* topic, byte* payload, unsigned int length) {
  // handle message arrived
}

EthernetClient ethClient;
PubSubClient client(server, 1883, callback, ethClient);

Blueline blueline;
Acurite5n1 acurite5n1;
Acurite592TX acurite592tx;

volatile word pulse;  //pulse duration

long previousMillis = 0;

char packet[100];

#if DPIN_OOK_RX >= 14
#define VECT PCINT1_vect
#elif DPIN_OOK_RX >= 8
#define VECT PCINT0_vect
#else
#define VECT PCINT2_vect
#endif

ISR(VECT) {
  PinChange();
}

static void setupPinChangeInterrupt ()
{
  pinMode(DPIN_OOK_RX, INPUT);
#if DPIN_OOK_RX >= 14
  bitSet(PCMSK1, DPIN_OOK_RX - 14);
  bitSet(PCICR, PCIE1);
#elif DPIN_OOK_RX >= 8
  bitSet(PCMSK0, DPIN_OOK_RX - 8);
  bitSet(PCICR, PCIE0);
#else
  PCMSK2 = bit(DPIN_OOK_RX);
  bitSet(PCICR, PCIE2);
#endif
}

void PinChange(void) {
    static word last;
    // determine the pulse length in microseconds, for either polarity
    pulse = micros() - last;
    last += pulse;
}

void reportSerial (const char* s, class DecodeOOK& decoder) {
    byte pos;
    const byte* data = decoder.getData(pos);
    
    Serial.print("["); Serial.print(millis() / 1000); Serial.print("] "); 
    for (byte i = 0; i < pos; ++i) {
      Serial.print(data[i] >> 4, HEX);
      Serial.print(data[i] & 0x0F, HEX);
    }
    Serial.print(' '); 
    Serial.println(s);
    
    decoder.resetDecoder();
}


void setup () {
    delay(250);  // delay  so that W5100 Ethernet chip
                 // has enough time to reset
    Serial.begin(38400);
    pinMode(DPIN_LED,OUTPUT);
    
    setupPinChangeInterrupt();
    
    Ethernet.begin(mac, ip);
    if (client.connect("arduinoClient")) {
      client.publish("ookDecoder", "online");
      client.publish("ookDecoder", VERSION);
      Serial.println("ookDecoder started");
      Serial.println(VERSION);
    }
}

void loop () {

    //may have issues with rollover
    unsigned long currentMillis = millis();
    if(currentMillis - previousMillis > REPORT_TIME) {
      previousMillis = currentMillis;  
      if (client.connect("arduinoClient")) {
        Serial.println("connected to arduinoClient");
        client.publish("ookDecoder","report");
        
        blueline.MQTTreport(packet);
        if (strlen(packet) > 0) {
          client.publish("blueline",packet);
          Serial.println(packet);
        }
        
        acurite5n1.MQTTreport(packet);
        if (strlen(packet) > 0) {
          client.publish("acurite5n1",packet);
          Serial.println(packet);
        }
        
        acurite592tx.MQTTreport(packet);
        if (strlen(packet) > 0) {
          client.publish("acurite592tx",packet);
          Serial.println(packet);
        }
      } else {
        Serial.println("connection failed");
      }
    }

    static int i = 0;
    word p;
    
    ATOMIC_BLOCK(ATOMIC_FORCEON)
    {
      p = pulse;
      pulse = 0;
    }
    
    if (p>150 && p<2000) {
      if (blueline.nextPulse(p)) {
        //turn on led
        digitalWrite(DPIN_LED, HIGH);
        //reportSerial("Blueline", blueline);
        blueline.decodeRxPacket();
        //blueline.PrintRaw();
        blueline.resetDecoder();
        digitalWrite(DPIN_LED,LOW);
      }
      if (acurite5n1.nextPulse(p)) {
        //turn on led
        digitalWrite(DPIN_LED, HIGH);
        //reportSerial("Acurite5n1", acurite5n1);
        acurite5n1.DecodePacket();
        //acurite5n1.PrintRaw();
        acurite5n1.resetDecoder();
        digitalWrite(DPIN_LED,LOW);
      }
      if (acurite592tx.nextPulse(p)) {
        //turn on led
        digitalWrite(DPIN_LED, HIGH);
        //reportSerial("Acurite592TX", acurite592tx);
        acurite592tx.DecodePacket();
        //acurite592tx.PrintRaw();
        acurite592tx.resetDecoder();
        digitalWrite(DPIN_LED,LOW);
      }
    }
}
