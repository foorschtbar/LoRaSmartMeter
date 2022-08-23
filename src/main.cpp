#include <Arduino.h>
#include <SoftwareSerial.h>
#include "sml.h"

#define rxPin D7

// SoftwareSerial IRSerial;
SoftwareSerial IRSerial(D7 /* RX */, D8 /* TX */);

sml_states_t currentState;
unsigned char currentChar = 0;
unsigned long counter = 0, transmitted = 0;
double T1Wh = -2, SumWh = -2, T1WhSend = -2, SumWhSend = -2;

typedef struct
{
  const unsigned char OBIS[6];
  void (*Handler)();
} OBISHandler;

void PowerT1()
{
  smlOBISWh(T1Wh);
}

void PowerSum()
{
  smlOBISWh(SumWh);
}

OBISHandler OBISHandlers[] = {
    {{0x01, 0x00, 0x01, 0x08, 0x01, 0xff}, &PowerT1},  /*   1-  0:  1.  8.1*255 (T1) */
    {{0x01, 0x00, 0x01, 0x08, 0x00, 0xff}, &PowerSum}, /*   1-  0:  1.  8.0*255 (T1 + T2) */
    {{0}, 0}};

void setup(){
  Serial.begin(9600);

  pinMode(rxPin, INPUT);
  IRSerial.begin(9600, SWSERIAL_8N1, rxPin, -1, false, 0, 95);
  IRSerial.enableRx(true);
  IRSerial.enableTx(false);
}

void readByte()
{
  unsigned int iHandler = 0;
  currentState = smlState(currentChar);
  if (currentState == SML_START)
  {
    /* reset local vars */
    T1Wh = -3;
    SumWh = -3;
  }
  if (currentState == SML_LISTEND)
  {
    /* check handlers on last received list */
    for (iHandler = 0; OBISHandlers[iHandler].Handler != 0 &&
                       !(smlOBISCheck(OBISHandlers[iHandler].OBIS));
         iHandler++)
      ;
    if (OBISHandlers[iHandler].Handler != 0)
    {
      OBISHandlers[iHandler].Handler();
    }
  }
  if (currentState == SML_UNEXPECTED)
  {
    Serial.print(F(">>> Unexpected byte!\n"));
  }
  if (currentState == SML_FINAL)
  {
    SumWhSend = SumWh;
    T1WhSend = T1Wh;
    // updateDisplay();
    counter++;
    Serial.print(F(">>> Successfully received a complete message!\n"));
  }
}

void loop()
{
  // os_runloop_once();
  if (IRSerial.available())
  {
    currentChar = IRSerial.read();
    // Serial.printf("%02X", currentChar);
    readByte();
  }
}

// void loop()
// {

// while (IRSerial.available() > 0)
// {
//   x = true;
//   char c = IRSerial.read();
//   // Serial.write(c);
//   Serial.printf("%c ", c);
// }
// if(x) {
//   Serial.println();
//   x = false;
// }
// }