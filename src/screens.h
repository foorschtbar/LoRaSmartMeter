#ifndef screens_h
#define screens_h

#include <U8g2lib.h>

class Screens
{
public:
    Screens(U8G2 &, const char *title, int, unsigned long, unsigned long);

    void displayMsgForce(const char *text, const char *text2 = "", const char *text3 = "", const char *text4 = "", const char *text5 = "");
    void displayMsg(const char *text, const char *text2 = "", const char *text3 = "", const char *text4 = "", const char *text5 = "");
    void nextScreen();
    void powerSave(bool activatePowerSave, bool force = false);
    void showScreen(int screenNumber);
    void dirty();
    void reset();
    void loop();
    void setup();

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
    bool _needRefresh;
    bool _modalMessageActive = false;

    unsigned long _lastScreenUpdate;
    unsigned long _lastScreenActivation;
    unsigned long _updateInterval;
    unsigned long _screenTimeout;
};

#endif
