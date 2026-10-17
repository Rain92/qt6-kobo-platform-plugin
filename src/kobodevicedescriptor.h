#ifndef KOBODEVICEDESCRIPTOR_H
#define KOBODEVICEDESCRIPTOR_H

#include <QtCore>

// NOTE: Apps include this header and receive the struct by value through
// KoboPlatformFunctions::getKoboDeviceDescriptor(), so they have to be rebuilt when it changes.
// New fields and enum values are only ever appended.

enum KoboDevice
{
    KoboUnknown,
    KoboTouchAB,
    KoboTouchC,
    KoboMini,
    KoboGlo,
    KoboGloHD,
    KoboTouch2,
    KoboAura,
    KoboAuraHD,
    KoboAuraH2O,
    KoboAuraH2O2_v1,
    KoboAuraH2O2_v2,
    KoboAuraOne,
    KoboAura2,
    KoboAura2_v2,
    KoboClaraHD,
    KoboForma,
    KoboLibraH2O,
    KoboNia,
    KoboElipsa,
    KoboSage,
    KoboLibra2,
    KoboClara2E,
    KoboElipsa2E,
    KoboLibraColour,
    KoboClaraBW,
    KoboClaraColour
};

struct TouchscreenSettings
{
    bool swapXY = true;
    bool invertX = true;
    bool invertY = false;
    bool hasMultitouch = true;
};

struct FrontlightSettings
{
    bool hasFrontLight = true;
    int frontlightMin = 0;
    int frontlightMax = 100;
    bool hasNaturalLight = false;
    bool hasNaturalLightMixer = false;
    bool naturalLightInverted = false;
    int naturalLightMin = 0;
    int naturalLightMax = 100;

    QString frontlightDevWhite = "/sys/class/backlight/mxc_msp430.0/brightness";
    QString frontlightDevMixer = "/sys/class/backlight/lm3630a_led/color";
    QString frontlightDevRed = "/sys/class/backlight/lm3630a_led1a";
    QString frontlightDevGreen = "/sys/class/backlight/lm3630a_ledb";
};

struct KoboDeviceDescriptor
{
    KoboDevice device = KoboUnknown;
    QString modelName;
    int modelNumber = 0;
    int mark = 6;
    int dpi = 0;
    int width = 0;
    int height = 0;
    qreal physicalWidth = 0;
    qreal physicalHeight = 0;

    bool hasKeys = false;
    bool canToggleChargingLED = false;

    // New devices *may* be REAGL-aware, but generally don't expect explicit REAGL requests, default to not.
    bool isREAGL = false;

    bool hasGSensor = false;

    // Filled in from FBInk's device database at runtime
    TouchscreenSettings touchscreenSettings;
    FrontlightSettings frontlightSettings;

    // MXCFB_WAIT_FOR_UPDATE_COMPLETE ioctls are generally reliable (filled in from FBInk)
    bool hasReliableMxcWaitFor = true;
    // Sunxi devices require a completely different fb backend...
    bool isSunxi = false;

    // Standard sysfs path to the battery directory
    QString batterySysfs = "/sys/class/power_supply/mc13892_bat";
    // Fallback paths, the input devices are normally found by probing their capabilities
    QString ntxDev = "/dev/input/event0";
    QString touchDev = "/dev/input/event1";
    QString powerDev = "null";

    // Appended in the Qt 6 port
    // MediaTek SoC (Clara 2E, Elipsa 2E, Libra Colour, Clara BW/Colour)
    bool isMTK = false;
    // Kaleido color panel
    bool hasColorPanel = false;
    bool hasStylus = false;
    // As reported by FBInk, e.g. "Daylight"
    QString codeName;
    QString platform;
};

KoboDeviceDescriptor determineDevice(bool debug = false);

#endif  // KOBODEVICEDESCRIPTOR_H
