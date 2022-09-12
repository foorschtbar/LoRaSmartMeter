#ifndef screens_h
#define screens_h

#include <U8g2lib.h>

#define SCREENS_LOCKSCREEN_UNSET_VALUE 255
#define PIN_TIMEOUT 1000
class Screens
{
public:
    Screens(U8G2 &, const char *title, int numofscreens, unsigned long updateInterval, unsigned long screenTimeout);

    void displayMsgForce(const char *text, const char *text2 = "", const char *text3 = "", const char *text4 = "", const char *text5 = "");
    void displayMsg(const char *text, const char *text2 = "", const char *text3 = "", const char *text4 = "", const char *text5 = "");
    void nextScreen();
    void powerSave(bool activatePowerSave, bool force = false);
    void showScreen(int screenNumber);
    void dirty();
    void reset();
    void loop();
    void setup();
    void setPIN(const byte *pin, int pinScreenNo);
    void getCurrentPIN(byte *pin, int *pinPos);
    void lockScreen();

    uint8_t currentScreen();
    bool needRefresh();
    int count();

private:
    U8G2 _u8g2;
    uint8_t _numofscreens;
    uint8_t _currentScreen;

    bool _displayPowerSaving;
    char _buff[255];
    char _title[20];
    byte _pin[4];
    bool _needRefresh;
    bool _modalMessageActive = false;
    bool _screenLocked = false;
    int _lockScreenNumber = 0;
    int _pinPos = 0;
    byte _currentPin[4];

    unsigned long _lastScreenUpdate;
    unsigned long _lastScreenActivation;
    unsigned long _updateInterval;
    unsigned long _screenTimeout;
    unsigned long _lastPINIncrement;
};

#endif
