#include "kobodevicedescriptor.h"

#include "fbink.h"

// Hardware details FBInk doesn't know about, c.f. KOReader's frontend/device/kobo/device.lua.
// Model identification, screen dpi and the touch panel quirks come from FBInk's device database.

static const QString LedWhiteMsp430 = "/sys/class/backlight/mxc_msp430.0/brightness";
static const QString MixerLm3630a = "/sys/class/backlight/lm3630a_led/color";
static const QString MixerTlc5947 = "/sys/class/backlight/tlc5947_bl/color";
static const QString MixerAw99703 = "/sys/class/leds/aw99703-bl_FL1/color";

// Natural light with a "color" mixer: warmth goes from 0 to 10, written inverted on most devices
static FrontlightSettings mixerFrontlight(const QString &mixer, bool inverted = true)
{
    FrontlightSettings fl;
    fl.hasNaturalLight = true;
    fl.hasNaturalLightMixer = true;
    fl.naturalLightInverted = inverted;
    fl.naturalLightMin = 0;
    fl.naturalLightMax = 10;
    fl.frontlightDevWhite = LedWhiteMsp430;
    fl.frontlightDevMixer = mixer;
    fl.frontlightDevRed = "";
    fl.frontlightDevGreen = "";
    return fl;
}

static FrontlightSettings noFrontlight()
{
    FrontlightSettings fl;
    fl.hasFrontLight = false;
    return fl;
}

static KoboDeviceDescriptor descriptorFor(int deviceId)
{
    KoboDeviceDescriptor d;

    switch (deviceId)
    {
        case DEVICE_KOBO_TOUCH_A:
        case DEVICE_KOBO_TOUCH_B:
            d.device = KoboTouchAB;
            d.mark = 3;
            d.dpi = 200;
            d.hasKeys = true;
            d.frontlightSettings = noFrontlight();
            break;

        case DEVICE_KOBO_TOUCH_C:
            d.device = KoboTouchC;
            d.mark = 4;
            d.dpi = 200;
            d.hasKeys = true;
            d.frontlightSettings = noFrontlight();
            break;

        case DEVICE_KOBO_MINI:
            d.device = KoboMini;
            d.mark = 4;
            d.dpi = 200;
            d.frontlightSettings = noFrontlight();
            break;

        case DEVICE_KOBO_GLO:
            d.device = KoboGlo;
            d.mark = 4;
            d.dpi = 212;
            break;

        case DEVICE_KOBO_GLO_HD:
        case DEVICE_TOLINO_SHINE_2HD:
            d.device = KoboGloHD;
            d.mark = 6;
            d.dpi = 300;
            break;

        case DEVICE_KOBO_TOUCH_2:
            d.device = KoboTouch2;
            d.mark = 6;
            d.dpi = 167;
            d.frontlightSettings = noFrontlight();
            break;

        case DEVICE_KOBO_AURA:
            d.device = KoboAura;
            d.mark = 5;
            d.dpi = 212;
            // NOTE: AFAICT, the Aura was the only one explicitly requiring REAGL requests...
            d.isREAGL = true;
            break;

        case DEVICE_KOBO_AURA_HD:
            d.device = KoboAuraHD;
            d.mark = 4;
            d.dpi = 265;
            break;

        case DEVICE_KOBO_AURA_H2O:
            d.device = KoboAuraH2O;
            d.mark = 5;
            d.dpi = 265;
            d.canToggleChargingLED = true;
            break;

        case DEVICE_KOBO_AURA_H2O_2:
            d.device = KoboAuraH2O2_v1;
            d.mark = 6;
            d.dpi = 265;
            d.frontlightSettings.hasNaturalLight = true;
            d.frontlightSettings.frontlightDevWhite = "/sys/class/backlight/lm3630a_ledb";
            d.frontlightSettings.frontlightDevRed = "/sys/class/backlight/lm3630a_led";
            d.frontlightSettings.frontlightDevGreen = "/sys/class/backlight/lm3630a_leda";
            break;

        case DEVICE_KOBO_AURA_H2O_2_R2:
            d.device = KoboAuraH2O2_v2;
            d.mark = 7;
            d.dpi = 265;
            d.frontlightSettings.hasNaturalLight = true;
            d.frontlightSettings.frontlightDevWhite = "/sys/class/backlight/lm3630a_ledb";
            d.frontlightSettings.frontlightDevRed = "/sys/class/backlight/lm3630a_leda";
            d.frontlightSettings.frontlightDevGreen = "";
            break;

        case DEVICE_KOBO_AURA_ONE:
        case DEVICE_KOBO_AURA_ONE_LE:
            d.device = KoboAuraOne;
            d.mark = 6;
            d.dpi = 300;
            d.frontlightSettings.hasNaturalLight = true;
            d.frontlightSettings.frontlightDevWhite = "/sys/class/backlight/lm3630a_led1b";
            d.frontlightSettings.frontlightDevRed = "/sys/class/backlight/lm3630a_led1a";
            d.frontlightSettings.frontlightDevGreen = "/sys/class/backlight/lm3630a_ledb";
            break;

        case DEVICE_KOBO_AURA_SE:
        case DEVICE_TOLINO_VISION:
            d.device = KoboAura2;
            d.mark = 6;
            d.dpi = 212;
            break;

        case DEVICE_KOBO_AURA_SE_R2:
            d.device = KoboAura2_v2;
            d.mark = 7;
            d.dpi = 212;
            break;

        case DEVICE_KOBO_CLARA_HD:
        case DEVICE_TOLINO_SHINE_3:
            d.device = KoboClaraHD;
            d.mark = 7;
            d.dpi = 300;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            break;

        // NOTE: Rotation events *may* not be enabled if Nickel has never been brought up in that power cycle.
        case DEVICE_KOBO_FORMA:
        case DEVICE_KOBO_FORMA_32GB:
        case DEVICE_TOLINO_EPOS_2:
            d.device = KoboForma;
            d.mark = 7;
            d.dpi = 300;
            d.hasKeys = true;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerTlc5947);
            break;

        case DEVICE_KOBO_LIBRA_H2O:
        case DEVICE_TOLINO_VISION_5:
            d.device = KoboLibraH2O;
            d.mark = 7;
            d.dpi = 300;
            d.hasKeys = true;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            break;

        case DEVICE_KOBO_NIA:
            d.device = KoboNia;
            d.mark = 7;
            d.dpi = 212;
            d.canToggleChargingLED = true;
            break;

        case DEVICE_KOBO_ELIPSA:
            d.device = KoboElipsa;
            d.mark = 8;
            d.dpi = 227;
            d.canToggleChargingLED = true;
            d.hasStylus = true;
            d.batterySysfs = "/sys/class/power_supply/battery";
            d.ntxDev = "/dev/input/by-path/platform-ntx_event0-event";
            d.touchDev = "/dev/input/by-path/platform-0-0010-event";
            break;

        case DEVICE_KOBO_SAGE:
        case DEVICE_TOLINO_EPOS_3:
            d.device = KoboSage;
            d.mark = 8;
            d.dpi = 300;
            d.hasKeys = true;
            d.canToggleChargingLED = true;
            d.hasStylus = true;
            // The only mixer that isn't inverted
            d.frontlightSettings = mixerFrontlight(MixerAw99703, false);
            d.batterySysfs = "/sys/class/power_supply/battery";
            d.ntxDev = "/dev/input/by-path/platform-ntx_event0-event";
            d.touchDev = "/dev/input/by-path/platform-0-0010-event";
            break;

        case DEVICE_KOBO_LIBRA_2:
            d.device = KoboLibra2;
            d.mark = 7;
            d.dpi = 300;
            d.hasKeys = true;
            d.canToggleChargingLED = true;
            // The mixer differs between hardware revisions, see probeFrontlightMixer()
            d.frontlightSettings = mixerFrontlight(MixerAw99703);
            d.batterySysfs = "/sys/class/power_supply/battery";
            break;

        case DEVICE_KOBO_CLARA_2E:
            d.device = KoboClara2E;
            d.mark = 7;
            d.dpi = 300;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerAw99703);
            d.batterySysfs = "/sys/class/power_supply/battery";
            d.powerDev = "/dev/input/by-path/platform-bd71828-pwrkey-event";
            break;

        case DEVICE_KOBO_ELIPSA_2E:
            d.device = KoboElipsa2E;
            d.mark = 7;
            d.dpi = 227;
            d.canToggleChargingLED = true;
            d.hasStylus = true;
            d.frontlightSettings = mixerFrontlight(MixerAw99703);
            d.batterySysfs = "/sys/class/power_supply/bd71827_bat";
            break;

        case DEVICE_KOBO_LIBRA_COLOUR:
        case DEVICE_TOLINO_VISION_COLOR:
            d.device = KoboLibraColour;
            d.mark = 7;
            d.dpi = 300;
            d.hasKeys = true;
            d.canToggleChargingLED = true;
            d.hasStylus = true;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            d.batterySysfs = "/sys/class/power_supply/bd71827_bat";
            break;

        case DEVICE_KOBO_CLARA_BW:
        case DEVICE_KOBO_CLARA_BW_TPV:
        case DEVICE_TOLINO_SHINE_BW:
            d.device = KoboClaraBW;
            d.mark = 7;
            d.dpi = 300;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            d.batterySysfs = "/sys/class/power_supply/bd71827_bat";
            break;

        case DEVICE_KOBO_CLARA_COLOUR:
        case DEVICE_TOLINO_SHINE_COLOR:
            d.device = KoboClaraColour;
            d.mark = 7;
            d.dpi = 300;
            d.canToggleChargingLED = true;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            d.batterySysfs = "/sys/class/power_supply/bd71827_bat";
            break;

        default:
            // Something newer than this plugin: FBInk still provides the screen and touch details,
            // the frontlight and battery are probed below.
            d.device = KoboUnknown;
            d.mark = 7;
            d.frontlightSettings = mixerFrontlight(MixerLm3630a);
            d.batterySysfs = "/sys/class/power_supply/battery";
            break;
    }

    return d;
}

// Same order as KOReader's automagic sysfs detection
static void probeBattery(KoboDeviceDescriptor &d)
{
    if (QFile::exists(d.batterySysfs))
        return;

    for (const char *path : {"/sys/class/power_supply/battery", "/sys/class/power_supply/bd71827_bat",
                             "/sys/class/power_supply/mc13892_bat"})
    {
        if (QFile::exists(path))
        {
            d.batterySysfs = path;
            return;
        }
    }
}

static void probeFrontlightMixer(KoboDeviceDescriptor &d)
{
    FrontlightSettings &fl = d.frontlightSettings;
    if (!fl.hasNaturalLightMixer || QFile::exists(fl.frontlightDevMixer))
        return;

    for (const QString &mixer : {MixerAw99703, MixerLm3630a, MixerTlc5947})
    {
        if (QFile::exists(mixer))
        {
            fl.frontlightDevMixer = mixer;
            return;
        }
    }

    // Unknown hardware with no known mixer: plain frontlight through /dev/ntx_io
    if (d.device == KoboUnknown)
    {
        fl.hasNaturalLight = false;
        fl.hasNaturalLightMixer = false;
    }
}

KoboDeviceDescriptor determineDevice(bool debug)
{
    // FBInk identifies the device from the NTX HWConfig block, which works on every model and firmware
    FBInkConfig cfg = {0};
    cfg.is_quiet = !debug;
    FBInkState state = {0};

    int fd = fbink_open();
    if (fd < 0 || fbink_init(fd, &cfg) < 0)
        qWarning("kobofb: FBInk failed to initialize, unable to identify the device");
    fbink_get_state(&cfg, &state);
    if (fd >= 0)
        fbink_close(fd);

    KoboDeviceDescriptor device = descriptorFor(state.device_id);

    device.modelNumber = state.device_id;
    device.codeName = QString::fromLatin1(state.device_codename);
    device.modelName = device.codeName.toLower();
    device.platform = QString::fromLatin1(state.device_platform);

    if (state.screen_dpi > 0)
        device.dpi = state.screen_dpi;
    if (device.dpi <= 0)
        device.dpi = 300;

    device.isSunxi = state.is_sunxi;
    device.isMTK = state.is_mtk;
    device.hasColorPanel = state.has_color_panel;
    device.hasGSensor = state.can_rotate;
    device.hasReliableMxcWaitFor = !state.unreliable_wait_for;

    device.touchscreenSettings.swapXY = state.touch_swap_axes;
    device.touchscreenSettings.invertX = state.touch_mirror_x;
    device.touchscreenSettings.invertY = state.touch_mirror_y;
    device.touchscreenSettings.hasMultitouch = !state.is_kobo_non_mt;

    // Updated once the screen is set up in its final rotation
    device.width = state.screen_width;
    device.height = state.screen_height;
    device.physicalWidth = qRound(device.width * 25.4 / device.dpi);
    device.physicalHeight = qRound(device.height * 25.4 / device.dpi);

    probeBattery(device);
    probeFrontlightMixer(device);

    if (device.device == KoboUnknown)
        qWarning("kobofb: Unknown device %s (%s, id %d), using generic settings", state.device_name,
                 state.device_codename, state.device_id);

    if (debug)
        qDebug() << "kobofb: Identified" << state.device_name << device.codeName << "id" << device.modelNumber
                 << "platform" << device.platform << "dpi" << device.dpi << "sunxi" << device.isSunxi << "mtk"
                 << device.isMTK << "color" << device.hasColorPanel;

    return device;
}
