#include <Arduino.h>
#include <SoftwareSerial.h>
#include <U8g2lib.h>
#include "sml.h"

#define PIN_RX 36
#define MAX_STR_MANUF 5
#define PIN_LED BUILTIN_LED

SoftwareSerial IRSerial;
// SoftwareSerial IRSerial(D7 /* RX */, D8 /* TX */);
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(/* clock=*/15, /* data=*/4, /* reset=*/16);

sml_states_t currentState;
unsigned char currentChar = 0;
unsigned long counter = 0, transmitted = 0, lastLED = 0;
char buffer[50];
char statusMsg[30] = "Unknown";
char floatBuffer[20];
double activePosEnergyTotal = -2, activeNegEnergyTotal = -2, sumActivePower = -2, sumActivePowerL1 = -2, sumActivePowerL2 = -2, sumActivePowerL3 = -2;
double activePosEnergyTotalSend = -2, activeNegEnergyTotalSend = -2, sumActivePowerSend = -2, sumActivePowerL1Send = -2, sumActivePowerL2Send = -2, sumActivePowerL3Send = -2;
unsigned char manuf[MAX_STR_MANUF];
bool ledState = false;

typedef struct
{
  const unsigned char OBIS[6];
  void (*Handler)();
} OBISHandler;

void clearIRSerialBuffer()
{
  while (IRSerial.available())
  {
    IRSerial.read();
  }
}

void Manufacturer()
{
  smlOBISManufacturer(manuf, MAX_STR_MANUF);
}

void ActivePosEnergyTotal()
{
  smlOBISWh(activePosEnergyTotal);
}

void ActiveNegEnergyTotal()
{
  smlOBISWh(activeNegEnergyTotal);
}

void SumActivePower()
{
  smlOBISW(sumActivePower);
}

void SumActivePowerL1()
{
  smlOBISW(sumActivePowerL1);
}

void SumActivePowerL2()
{
  smlOBISW(sumActivePowerL2);
}

void SumActivePowerL3()
{
  smlOBISW(sumActivePowerL3);
}

OBISHandler OBISHandlers[] = {
    {{0x81, 0x81, 0xc7, 0x82, 0x03, 0xff}, &Manufacturer},         // 8181c78203ff (129-129:199.130.3*255)
    {{0x01, 0x00, 0x01, 0x08, 0x00, 0xff}, &ActivePosEnergyTotal}, // 0100010800ff (1-0:1.8.0*255) (Positive active energy (A+) total [kWh])
    {{0x01, 0x00, 0x02, 0x08, 0x00, 0xff}, &ActiveNegEnergyTotal}, // 0100020800ff (1-0:2.8.0*255) (Negative active energy (A-) total [kWh])
    {{0x01, 0x00, 0x10, 0x07, 0x00, 0xff}, &SumActivePower},       // 0100100700ff (1-0:16.7.0*255) (Sum active instantaneous power (A+ - A-) [kW])
    {{0x01, 0x00, 0x24, 0x07, 0x00, 0xff}, &SumActivePowerL1},     // 0100240700ff (1-0:36.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L1 [kW])
    {{0x01, 0x00, 0x38, 0x07, 0x00, 0xff}, &SumActivePowerL2},     // 0100380700ff (1-0:56.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L2 [kW])
    {{0x01, 0x00, 0x4c, 0x07, 0x00, 0xff}, &SumActivePowerL3},     // 01004c0700ff (1-0:76.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L3 [kW])
    {{0}, 0}};

void printHex2(unsigned v)
{
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

void updateDisplay()
{
  u8x8.drawString(0, 2, statusMsg);

  sprintf(buffer, "Msg: %lu", counter);
  u8x8.drawString(0, 3, buffer);

  dtostrf(activePosEnergyTotal, 10, 1, floatBuffer);
  sprintf(buffer, "Sum: %s", floatBuffer);
  u8x8.drawString(0, 4, buffer);

  dtostrf(sumActivePower, 10, 1, floatBuffer);
  sprintf(buffer, "Watt: %s", floatBuffer);
  u8x8.drawString(0, 5, buffer);

  sprintf(buffer, "Tns: %lu", transmitted);
  u8x8.drawString(0, 6, buffer);
}

void setup()
{
  Serial.begin(115200);
  Serial.println("Starting");

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.drawString(0, 1, "SML to LoraWAN");
  u8x8.drawString(0, 2, "Waiting");

  pinMode(PIN_LED, OUTPUT);

  // // LMIC init
  // os_init();

  // // Reset the MAC state. Session and pending data transfers will be discarded.
  // LMIC_reset();

  pinMode(PIN_RX, INPUT);
  IRSerial.begin(9600, SWSERIAL_8N1, PIN_RX, -1, false, 0, 95);
  IRSerial.enableRx(true);
  IRSerial.enableTx(false);

  // do_send(&sendjob);
}

void setStatusLED(bool on)
{
  digitalWrite(PIN_LED, on ? HIGH : LOW);
  ledState = on;
  lastLED = millis();
}

void readByte()
{
  unsigned int iHandler = 0;
  currentState = smlState(currentChar);
  if (currentState == SML_START)
  {
    /* reset local vars */
    activePosEnergyTotal = -3;
    activeNegEnergyTotal = -3;
    sumActivePower = -3;
    sumActivePowerL1 = -3;
    sumActivePowerL2 = -3;
    sumActivePowerL3 = -3;
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
#ifdef SML_DEBUG
  if (currentState == SML_UNEXPECTED)
  {
    Serial.print(F(">>> Unexpected byte!\n"));
  }
#endif
  if (currentState == SML_FINAL)
  {

    setStatusLED(true);

    activePosEnergyTotalSend = activePosEnergyTotal;
    activeNegEnergyTotalSend = activeNegEnergyTotal;
    sumActivePowerSend = sumActivePower;
    sumActivePowerL1Send = sumActivePowerL1;
    sumActivePowerL2Send = sumActivePowerL2;
    sumActivePowerL3Send = sumActivePowerL3;

    updateDisplay();

    printf(">>> FINAL! Checksum OK. Counter %05lu.\n", counter);
    printf(">>> A+ total:                     %.3f kW\n", activePosEnergyTotalSend);
    printf(">>> A- total:                     %.3f kW\n", activeNegEnergyTotalSend);
    printf(">>> Active power (A+ - A-):       %.3f W\n", sumActivePowerSend);
    printf(">>> Active power (A+ - A-) in L1: %.3f W\n", sumActivePowerL1Send);
    printf(">>> Active power (A+ - A-) in L2: %.3f W\n", sumActivePowerL2Send);
    printf(">>> Active power (A+ - A-) in L3: %.3f W\n\n", sumActivePowerL3Send);

    counter++;
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

  if (ledState && millis() - lastLED > 200)
  {
    setStatusLED(false);
  }
}
