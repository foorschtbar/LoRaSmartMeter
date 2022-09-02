#include <Arduino.h>
#include <SoftwareSerial.h>
#include <U8g2lib.h>
#include <lmic.h>
#include <hal/hal.h>
#include <sml.h>
#include <keys.h>

// ++++++++++++++++++++++++++++++++++++++++
//
// CONSTANTS
//
// ++++++++++++++++++++++++++++++++++++++++

// LoRa module pins
#define LORA_DIO0 26
#define LORA_DIO1 33
#define LORA_DIO2 32
#define LORA_RESET 14
#define LORA_CS 18

// OLED Pins
#define OLED_SCL 15
#define OLED_SDA 4
#define OLED_RST 16

// Other pins
#define PIN_RX 36
#define PIN_LED BUILTIN_LED

// SML_Parser
#define MAX_STR_MANUF 5

// ++++++++++++++++++++++++++++++++++++++++
//
// LIBS
//
// ++++++++++++++++++++++++++++++++++++++++

SoftwareSerial IRSerial;
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(OLED_SCL, OLED_SDA, OLED_RST);

// ++++++++++++++++++++++++++++++++++++++++
//
// VARS
//
// ++++++++++++++++++++++++++++++++++++++++

// LoRa LMIC
static osjob_t sendjob;
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RESET,
    .dio = {LORA_DIO0, LORA_DIO1, LORA_DIO2},
};

// Schedule TX every this many seconds
const unsigned TX_INTERVAL = 60;

sml_states_t currentState;
unsigned char currentChar = 0;
unsigned long counter = 0, transmitted = 0, lastLED = 0, prepareCount = 0;
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

// ++++++++++++++++++++++++++++++++++++++++
//
// MAIN CODE
//
// ++++++++++++++++++++++++++++++++++++++++

void os_getArtEui(u1_t *buf) { memcpy_P(buf, APPEUI, 8); }
void os_getDevEui(u1_t *buf) { memcpy_P(buf, DEVEUI, 8); }
void os_getDevKey(u1_t *buf) { memcpy_P(buf, APPKEY, 16); }

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

void printHex(byte buffer[], size_t arraySize)
{
  unsigned c;
  for (size_t i = 0; i < arraySize; i++)
  {
    c = buffer[i];

    if (i != 0)
      Serial.write(32); // Space

    c &= 0xff;
    if (c < 16)
      Serial.write(48); // 0
    Serial.print(c, HEX);
  }
}

void updateDisplay()
{
  u8x8.drawString(0, 2, "                                 ");
  u8x8.drawString(0, 2, statusMsg);

  dtostrf(counter, 11, 0, floatBuffer);
  sprintf(buffer, "Msg: %s", floatBuffer);
  u8x8.drawString(0, 3, buffer);

  dtostrf(activePosEnergyTotal, 11, 2, floatBuffer);
  sprintf(buffer, "Sum: %s", floatBuffer);
  u8x8.drawString(0, 4, buffer);

  dtostrf(sumActivePower, 10, 2, floatBuffer);
  sprintf(buffer, "Watt: %s", floatBuffer);
  u8x8.drawString(0, 5, buffer);

  dtostrf(transmitted, 11, 0, floatBuffer);
  sprintf(buffer, "Tns: %s", floatBuffer);
  u8x8.drawString(0, 6, buffer);
}

void toByteArray(long long val, byte *byteArray, int startIndex, int length)
{
  for (int i = 0; i < length; i++)
  {
    byteArray[startIndex + i] = (val >> (8 * (length - 1 - i))) & 0xff;
  }

  // byteArray[startIndex] = (val >> 56) & 0xff;
  // byteArray[startIndex + 1] = (val >> 48) & 0xff;
  // byteArray[startIndex + 2] = (val >> 40) & 0xff;
  // byteArray[startIndex + 3] = (val >> 32) & 0xff;
  // byteArray[startIndex + 4] = (val >> 24) & 0xff;
  // byteArray[startIndex + 5] = (val >> 16) & 0xff;
  // byteArray[startIndex + 6] = (val >> 8) & 0xff;
  // byteArray[startIndex + 7] = val & 0xff;
}

void do_send(osjob_t *j)
{
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND)
  {
    Serial.println(F("OP_TXRXPEND, not sending"));
  }
  else
  {
    Serial.printf("Prepare LoRa package #%lu\n", counter);
    // Version                        1 byte
    // A+                             8 byte
    // A-                             8 byte
    // Active power(A + -A -)         8 byte
    // Active power(A + -A -) in L1   8 byte
    // Active power(A + -A -) in L2   8 byte
    // Active power(A + -A -) in L3   8 byte
    //                                ------
    //                                49 byte

    byte buffer[49];
    buffer[0] = (VERSION_MAJOR << 4) | (VERSION_MINOR & 0xf);
    toByteArray((activePosEnergyTotalSend * 1000), buffer, 1, 8);
    toByteArray((activeNegEnergyTotalSend * 1000), buffer, 9, 8);
    toByteArray((sumActivePowerSend * 1000), buffer, 17, 8);
    toByteArray((sumActivePowerL1Send * 1000), buffer, 25, 8);
    toByteArray((sumActivePowerL2Send * 1000), buffer, 33, 8);
    toByteArray((sumActivePowerL3Send * 1000), buffer, 41, 8);

    Serial.printf("> Payload: ");
    printHex(buffer, sizeof(buffer));
    Serial.println();
    Serial.printf("> Package queued\n\n");

    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);
  }
  // Next TX is scheduled after TX_COMPLETE event.
}

void onEvent(ev_t ev)
{
  Serial.print("LMIC Event: ");
  switch (ev)
  {
  case EV_SCAN_TIMEOUT:
    Serial.println(F("EV_SCAN_TIMEOUT"));
    break;
  case EV_BEACON_FOUND:
    Serial.println(F("EV_BEACON_FOUND"));
    break;
  case EV_BEACON_MISSED:
    Serial.println(F("EV_BEACON_MISSED"));
    break;
  case EV_BEACON_TRACKED:
    Serial.println(F("EV_BEACON_TRACKED"));
    break;
  case EV_JOINING:
    Serial.println(F("EV_JOINING"));
    break;
  case EV_JOINED:
    Serial.println(F("EV_JOINED"));
    {
      u4_t netid = 0;
      devaddr_t devaddr = 0;
      u1_t nwkKey[16];
      u1_t artKey[16];
      LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
      Serial.print("> NetID:   ");
      Serial.println(netid, DEC);
      Serial.print("> DevAddr: ");
      Serial.println(devaddr, HEX);
      Serial.print("> AppSKey: ");
      printHex(artKey, sizeof(artKey));
      Serial.println("");
      Serial.print("> NwkSKey: ");
      printHex(nwkKey, sizeof(nwkKey));
      Serial.println("\n");
      sprintf(statusMsg, "Joined");
      updateDisplay();
    }
    // Disable link check validation (automatically enabled
    // during join, but because slow data rates change max TX
    // size, we don't use it in this example.
    LMIC_setLinkCheckMode(0);

    // Start first transmission after join
    do_send(&sendjob);
    break;
  /*
  || This event is defined but not used in the code. No
  || point in wasting codespace on it.
  ||
  || case EV_RFU1:
  ||     Serial.println(F("EV_RFU1"));
  ||     break;
  */
  case EV_JOIN_FAILED:
    Serial.println(F("EV_JOIN_FAILED"));
    sprintf(statusMsg, "Join failed");
    break;
  case EV_REJOIN_FAILED:
    Serial.println(F("EV_REJOIN_FAILED"));
    sprintf(statusMsg, "rejoin failed");
    break;
  case EV_TXCOMPLETE:
    Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("Received ack"));
    if (LMIC.dataLen)
    {
      Serial.print(F("Received "));
      Serial.print(LMIC.dataLen);
      Serial.println(F(" bytes of payload"));
    }
    // Schedule next transmission
    os_setTimedCallback(&sendjob, os_getTime() + sec2osticks(TX_INTERVAL),
                        do_send);
    transmitted++;
    sprintf(statusMsg, "TX complete");
    updateDisplay();
    break;
  case EV_LOST_TSYNC:
    sprintf(statusMsg, "Lost sync");
    updateDisplay();
    Serial.println(F("EV_LOST_TSYNC"));
    break;
  case EV_RESET:
    sprintf(statusMsg, "Reset sync");
    updateDisplay();
    Serial.println(F("EV_RESET"));
    break;
  case EV_RXCOMPLETE:
    // data received in ping slot
    Serial.println(F("EV_RXCOMPLETE"));
    break;
  case EV_LINK_DEAD:
    sprintf(statusMsg, "Link dead");
    updateDisplay();
    Serial.println(F("EV_LINK_DEAD"));
    break;
  case EV_LINK_ALIVE:
    sprintf(statusMsg, "Link alive");
    updateDisplay();
    Serial.println(F("EV_LINK_ALIVE"));
    break;
  /*
  || This event is defined but not used in the code. No
  || point in wasting codespace on it.
  ||
  || case EV_SCAN_FOUND:
  ||    Serial.println(F("EV_SCAN_FOUND"));
  ||    break;
  */
  case EV_TXSTART:
    sprintf(statusMsg, "TX start");
    updateDisplay();
    Serial.println(F("EV_TXSTART"));
    break;
  case EV_TXCANCELED:
    Serial.println(F("EV_TXCANCELED"));
    break;
  case EV_RXSTART:
    /* do not print anything -- it wrecks timing */
    break;
  case EV_JOIN_TXCOMPLETE:
    Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
    break;

  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
  }
  Serial.println();
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

    printf("SML Decoded! Checksum OK (#%lu)\n", ++prepareCount);
    printf("> A+ total:                     %.3f kW\n", activePosEnergyTotalSend);
    printf("> A- total:                     %.3f kW\n", activeNegEnergyTotalSend);
    printf("> Active power (A+ - A-):       %.3f W\n", sumActivePowerSend);
    printf("> Active power (A+ - A-) in L1: %.3f W\n", sumActivePowerL1Send);
    printf("> Active power (A+ - A-) in L2: %.3f W\n", sumActivePowerL2Send);
    printf("> Active power (A+ - A-) in L3: %.3f W\n\n", sumActivePowerL3Send);

    counter++;
  }
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\n= Starting LoRaSmartMeter v%d.%d =\n\n", VERSION_MAJOR, VERSION_MINOR);

  u8x8.begin();
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.drawString(0, 1, "LoRaSmartMeter");
  u8x8.drawString(0, 2, "Waiting...");

  pinMode(PIN_LED, OUTPUT);

  // // LMIC init
  os_init();

  // // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // Software serial setup
  pinMode(PIN_RX, INPUT);
  IRSerial.begin(9600, SWSERIAL_8N1, PIN_RX, -1, false, 0, 95);
  IRSerial.enableRx(true);
  IRSerial.enableTx(false);

  // Start joining...
  LMIC_startJoining();
}

void loop()
{
  os_runloop_once();
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
