#include <Arduino.h>
#include <U8g2lib.h>
#include <lmic.h>
#include <hal/hal.h>
#include <sml.h>
#include <keys.h>
#include <screens.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <Update.h>
#include <webcontent.h>

#ifdef FAKE_SML
#include <testdata.h>
unsigned int testdata_pos = 0;
unsigned long lastTestMillis = 0;
#endif

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
#define PIN_BUTTON 39
#define PIN_LED 25 // BUILTIN_LED // 38

// SML_Parser
#define MAX_STR_MANUF 5

#define DEFAULT_STATUS_MSG "Unknown"
#define TX_INTERVAL 1000 * 60 // Schedule TX every this many (milli)seconds

// SCREEN Stuff
#define SCREEN_COUNT 3
#define DISPLAY_UPDATE_INTERVAL 200   // 0 no auto update. only if screen is "dirty"
#define DISPLAY_TIMEOUT 1000 * 60 * 1 // time after display will go offs
#define WIFI_TIMEOUT 1000 * 60 * 5    // time after wifi will be disabled
#define SML_DISPLAY_TIMEOUT 5000
#define TIME_BUTTON_LONGPRESS 10000

// ++++++++++++++++++++++++++++++++++++++++
//
// OBJECTS
//
// ++++++++++++++++++++++++++++++++++++++++

HardwareSerial IRSerial(1);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/OLED_RST, /* clock=*/OLED_SCL, /* data=*/OLED_SDA);
Screens screen(u8g2, "LoRaSmartMeter", SCREEN_COUNT, DISPLAY_UPDATE_INTERVAL, DISPLAY_TIMEOUT);
WebServer server(80);

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

sml_states_t currentState;
unsigned char currentChar = 0;
unsigned long counter = 0, LoRaTransmitted = 0, LoRaReceived = 0, lastLED = 0, lmicTxPrevMillis = 0, lastSMLsuccess = 0, lastDisplayUpdate = 0, lastButtonTimer = 0, lastWifiTimer = 0;
bool previousButtonState = HIGH; // will store last Button state. 1 = unpressed, 0 = pressed
char buffer[50];
char statusMsg[30] = DEFAULT_STATUS_MSG;
char statusMsgPrev[30];
char floatBuffer[20];
double voltL1 = -2, voltL2 = -2, voltL3 = -2, activePosEnergyTotal = -2, activeNegEnergyTotal = -2, sumActivePower = -2, sumActivePowerL1 = -2, sumActivePowerL2 = -2, sumActivePowerL3 = -2;
double voltL1Send = -2, voltL2Send = -2, voltL3Send = -2, activePosEnergyTotalSend = -2, activeNegEnergyTotalSend = -2, sumActivePowerSend = -2, sumActivePowerL1Send = -2, sumActivePowerL2Send = -2, sumActivePowerL3Send = -2;
unsigned char manuf[MAX_STR_MANUF];
bool ledState = false;
bool lmicIsIdle = true;
bool isDisplayUnlocked = false;
int currentScreen = 0;
bool displayPowerSaving = true;
bool wifiEnabled = false;

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

void VoltageL1()
{
  smlOBISVolt(voltL1);
}

void VoltageL2()
{
  smlOBISVolt(voltL2);
}

void VoltageL3()
{
  smlOBISVolt(voltL3);
}

OBISHandler OBISHandlers[] = {
    {{0x81, 0x81, 0xc7, 0x82, 0x03, 0xff}, &Manufacturer},         // 8181c78203ff (129-129:199.130.3*255)
    {{0x01, 0x00, 0x01, 0x08, 0x00, 0xff}, &ActivePosEnergyTotal}, // 0100010800ff (1-0:1.8.0*255) (Positive active energy (A+) total [kWh])
    {{0x01, 0x00, 0x02, 0x08, 0x00, 0xff}, &ActiveNegEnergyTotal}, // 0100020800ff (1-0:2.8.0*255) (Negative active energy (A-) total [kWh])
    {{0x01, 0x00, 0x10, 0x07, 0x00, 0xff}, &SumActivePower},       // 0100100700ff (1-0:16.7.0*255) (Sum active instantaneous power (A+ - A-) [kW])
    {{0x01, 0x00, 0x24, 0x07, 0x00, 0xff}, &SumActivePowerL1},     // 0100240700ff (1-0:36.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L1 [kW])
    {{0x01, 0x00, 0x38, 0x07, 0x00, 0xff}, &SumActivePowerL2},     // 0100380700ff (1-0:56.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L2 [kW])
    {{0x01, 0x00, 0x4c, 0x07, 0x00, 0xff}, &SumActivePowerL3},     // 01004c0700ff (1-0:76.7.0*255) (Sum active instantaneous power (A+ - A-) in phase L3 [kW])
    {{0x01, 0x00, 0x20, 0x07, 0x00, 0xff}, &VoltageL1},            // 0100200700ff (1-0:32.7.0*255) (Instantaneous voltage in phase I)
    {{0x01, 0x00, 0x34, 0x07, 0x00, 0xff}, &VoltageL2},            // 0100340700ff (1-0:52.7.0*255) (Instantaneous voltage in phase II)
    {{0x01, 0x00, 0x48, 0x07, 0x00, 0xff}, &VoltageL3},            // 0100480700ff (1-0:72.7.0*255) (Instantaneous voltage in phase III)
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

void setStatusMsg(const char *msg)
{
  sprintf(statusMsg, msg);
  screen.dirty();
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
    Serial.printf("Prepare LoRa package #%lu\n", LoRaTransmitted + 1);
    // Version                        1 byte
    // A+                             8 byte
    // A-                             8 byte
    // Active power(A + -A -)         4 byte
    // Active power(A + -A -) in L1   4 byte
    // Active power(A + -A -) in L2   4 byte
    // Active power(A + -A -) in L3   4 byte
    // Voltage in L1                  4 byte
    // Voltage in L2                  4 byte
    // Voltage in L3                  4 byte
    //                                ------
    //                                45 byte

    byte buffer[45];
    buffer[0] = (VERSION_MAJOR << 4) | (VERSION_MINOR & 0xf);
    toByteArray((activePosEnergyTotalSend * 1000), buffer, 1, 8);
    toByteArray((activeNegEnergyTotalSend * 1000), buffer, 9, 8);
    toByteArray((sumActivePowerSend * 1000), buffer, 17, 4);
    toByteArray((sumActivePowerL1Send * 1000), buffer, 21, 4);
    toByteArray((sumActivePowerL2Send * 1000), buffer, 25, 4);
    toByteArray((sumActivePowerL3Send * 1000), buffer, 29, 4);
    toByteArray((voltL1Send * 1000), buffer, 33, 4);
    toByteArray((voltL2Send * 1000), buffer, 37, 4);
    toByteArray((voltL3Send * 1000), buffer, 41, 4);

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
  lmicIsIdle = false;
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
    setStatusMsg("Joining...");
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

      setStatusMsg("Joined");
    }

    // Disable link check validation
    LMIC_setLinkCheckMode(0);
    // Disable ADR
    LMIC_setAdrMode(0);

    lmicIsIdle = true;

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
    setStatusMsg("Join failed");
    break;
  case EV_REJOIN_FAILED:
    Serial.println(F("EV_REJOIN_FAILED"));
    setStatusMsg("Rejoin failed");
    break;
  case EV_TXCOMPLETE:
    Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("Received ack"));
    if (LMIC.dataLen)
    {
      LoRaReceived++;
      Serial.print(F("Received "));
      Serial.print(LMIC.dataLen);
      Serial.println(F(" bytes of payload"));
    }

    LoRaTransmitted++;
    setStatusMsg("TX complete");
    lmicIsIdle = true;

    lmicTxPrevMillis = millis();
    break;
  case EV_LOST_TSYNC:
    setStatusMsg("Lost sync");

    Serial.println(F("EV_LOST_TSYNC"));
    break;
  case EV_RESET:
    setStatusMsg("Reset sync");

    Serial.println(F("EV_RESET"));
    break;
  case EV_RXCOMPLETE:
    // data received in ping slot
    Serial.println(F("EV_RXCOMPLETE"));
    break;
  case EV_LINK_DEAD:
    setStatusMsg("Link dead");

    Serial.println(F("EV_LINK_DEAD"));
    break;
  case EV_LINK_ALIVE:
    setStatusMsg("Link alive");

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
    setStatusMsg("TX start");

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
    voltL1 = -3;
    voltL2 = -3;
    voltL3 = -3;
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
    voltL1Send = voltL1;
    voltL2Send = voltL2;
    voltL3Send = voltL3;

    screen.dirty();

    printf("SML Decoded! Checksum OK (#%lu)\n", ++counter);
    printf("> A+ total:                     %.3f Wh\n", activePosEnergyTotalSend);
    printf("> A- total:                     %.3f Wh\n", activeNegEnergyTotalSend);
    printf("> Active power (A+ - A-):       %.3f W\n", sumActivePowerSend);
    printf("> Active power (A+ - A-) in L1: %.3f W\n", sumActivePowerL1Send);
    printf("> Active power (A+ - A-) in L2: %.3f W\n", sumActivePowerL2Send);
    printf("> Active power (A+ - A-) in L3: %.3f W\n", sumActivePowerL3Send);
    printf("> Voltage L1:                   %.1f V\n", voltL1Send);
    printf("> Voltage L2:                   %.1f V\n", voltL2Send);
    printf("> Voltage L3:                   %.1f V\n", voltL3Send);
    printf("\n");

    lastSMLsuccess = millis();
  }
}

void handleDisplay()
{

  if (screen.needRefresh())
  {

    char buff1[255], buff2[255], buff3[255], buff4[255], buff5[255];
    signed long nextLoRaTx;

    switch (screen.currentScreen())
    {
    case 1:
      snprintf(buff1, sizeof(buff1), "SML RX %14i", counter);

      // activePosEnergyTotalSend
      if (activePosEnergyTotalSend > 0)
      {
        snprintf(buff2, sizeof(buff2), "A+ %15.3f Wh", activePosEnergyTotalSend);
      }
      else
      {
        snprintf(buff2, sizeof(buff2), "A+ %18s", "---");
      }

      // activeNegEnergyTotalSend
      if (activeNegEnergyTotalSend > 0)
      {
        snprintf(buff3, sizeof(buff3), "A- %15.3f Wh", activeNegEnergyTotalSend);
      }
      else
      {
        snprintf(buff3, sizeof(buff3), "A- %18s", "---");
      }

      // sumActivePowerSend
      if (sumActivePowerSend > 0)
      {
        snprintf(buff4, sizeof(buff4), "Watt %14.2f W", sumActivePowerSend);
      }
      else
      {
        snprintf(buff4, sizeof(buff4), "Watt %16s", "---");
      }

      // Voltages
      if (sumActivePowerSend > 0)
      {
        snprintf(buff5, sizeof(buff5), "Volt %14.2f V", voltL1Send);
      }
      else
      {
        snprintf(buff5, sizeof(buff5), "Volt %16s", "---");
      }
      screen.displayMsg(buff1, buff2, buff3, buff4, buff5);
      break;

    case 2:
      snprintf(buff1, sizeof(buff1), "Status %14s", statusMsg);
#ifdef LORA_OFF
      snprintf(buff2, sizeof(buff2), "LoRa turned off!");
#else
      nextLoRaTx = (TX_INTERVAL - (millis() - lmicTxPrevMillis));
      snprintf(buff2, sizeof(buff2), "Next TX %12lus", (nextLoRaTx > 0 ? nextLoRaTx / 1000 : 0));
#endif
      snprintf(buff3, sizeof(buff3), "TX# %17i", LMIC.seqnoUp);
      snprintf(buff4, sizeof(buff4), "RX# %17i", LMIC.seqnoDn);
      screen.displayMsg(buff1, buff2, buff3, buff4, "");
      break;

    case 3:
      snprintf(buff1, sizeof(buff1), "Firmware v%d.%d", VERSION_MAJOR, VERSION_MINOR);
      snprintf(buff3, sizeof(buff3), " %s %s", __DATE__, __TIME__);
      screen.displayMsg(buff1, "Compiled:", buff3, "(c) 2022 foorschtbar", "");
      break;

    // PIN/Login Screen
    case 4:
      byte currentPin[4];
      int pinPos = 0;
      screen.getCurrentPIN(currentPin, &pinPos);

      char pinstr[4];
      for (int i = 0; i < 4; i++)
      {
        if (i == pinPos && currentPin[i] == SCREENS_LOCKSCREEN_UNSET_VALUE)
        {
          pinstr[i] = '_';
        }
        else if (i <= pinPos && currentPin[i] != SCREENS_LOCKSCREEN_UNSET_VALUE)
        {
          pinstr[i] = currentPin[i] + '0';
        }
        else
        {
          pinstr[i] = '-';
        }
      }

      snprintf(buff2, sizeof(buff2), "    PIN: %c %c %c %c", pinstr[0], pinstr[1], pinstr[2], pinstr[3]);
      screen.displayMsg("", buff2, "", "", "");
      break;
    }
  }
}

void startWiFiAP()
{
  if (!wifiEnabled)
  {
    wifiEnabled = true;

    screen.displayMsgForce("Starting WiFi AP", "Please wait...");
    WiFi.softAP(WIFI_SSID, WIFI_PSK);

    // Index page
    server.on("/", HTTP_GET, []()
              {
              lastWifiTimer = millis();
              server.sendHeader("Connection", "close");
              server.send(200, "text/html", serverIndex); });

    // Firmware uploading
    server.on(
        "/update", HTTP_POST, []()
        {
          lastWifiTimer = millis();
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart(); },
        []()
        {
          HTTPUpload &upload = server.upload();
          if (upload.status == UPLOAD_FILE_START)
          {
            Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN))
            { // start with max available size
              Update.printError(Serial);
            }
          }
          else if (upload.status == UPLOAD_FILE_WRITE)
          {
            /* flashing firmware to ESP*/
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize)
            {
              Update.printError(Serial);
            }
          }
          else if (upload.status == UPLOAD_FILE_END)
          {
            if (Update.end(true))
            { // true to set the size to the current progress
              Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
            }
            else
            {
              Update.printError(Serial);
            }
          }
        });
    server.begin();

    char buffer[255];
    snprintf(buffer, sizeof(buffer), "http://%s", WiFi.softAPIP().toString().c_str());
    screen.displayMsgForce("WiFi AP Ready", "Connect now to", WIFI_SSID, "and open", buffer);
    lastWifiTimer = millis();
  }
}

void handleButton()
{
  static bool prevBntState;
  bool btnState = digitalRead(PIN_BUTTON);
  if (btnState == LOW) // pressed
  {
    if (btnState != prevBntState) // was not pressed and is now pressed
    {
      printf("[handleButton] Button pressed short\n");

      lastButtonTimer = millis();
      screen.nextScreen();

      printf("[handleButton] Current screen: %i\n", screen.currentScreen());
    }

    if ((millis() - lastButtonTimer >= TIME_BUTTON_LONGPRESS))
    {
      printf("[handleButton] Button pressed long\n");
      startWiFiAP();
    }

    // Delay a little bit to avoid bouncing
    delay(50);
  }
  prevBntState = btnState;
}

void setup()
{
  Serial.begin(115200);
  Serial.printf("\n == Starting LoRaSmartMeter v%d.%d ==\n\n", VERSION_MAJOR, VERSION_MINOR);

  // Display
  screen.setup();
  screen.displayMsgForce("Booting. Please wait...");
  delay(1000);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);

#ifndef LORA_OFF
  // // LMIC init
  os_init();

  // // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();
#endif

  // 2nd Hardware serial setup
  pinMode(PIN_RX, INPUT);
  IRSerial.begin(9600, SERIAL_8N1, PIN_RX, -1, false);

  // Screen stuff
  screen.setPIN(DISPLAY_PIN, 4);
  screen.showScreen(4);

#ifndef LORA_OFF
  // Start joining...
  LMIC_startJoining();
#endif
}

void loop()
{
#ifndef LORA_OFF
  os_runloop_once();
#endif

  // Display
  screen.loop();
  handleDisplay();

  // Button
  handleButton();

#ifdef FAKE_SML
  if (lastTestMillis == 0 || millis() - lastTestMillis > 1000)
  {
    currentChar = testdata[testdata_pos];
    testdata_pos++;
    if (testdata_pos == testdata_len)
    {
      testdata_pos = 0;
      lastTestMillis = millis();
    }
    readByte();
  }
#else
  if (IRSerial.available())
  {
    currentChar = IRSerial.read();
    // Serial.printf("%02X", currentChar);
    readByte();
  }
#endif

  if (ledState && millis() - lastLED > 200)
  {
    setStatusLED(false);
  }

  if (lmicIsIdle && (millis() - lmicTxPrevMillis) >= TX_INTERVAL)
  {
    lmicIsIdle = false;
#ifndef LORA_OFF
    do_send(&sendjob);
#endif
  }

  if (wifiEnabled)
  {
    // WiFi AP
    server.handleClient();

    // Disable WiFi AP after x Minutes of inactivity
    if ((millis() - lastWifiTimer) >= WIFI_TIMEOUT)
    {
      wifiEnabled = false;
      WiFi.mode(WIFI_OFF);
    }
  }
}
