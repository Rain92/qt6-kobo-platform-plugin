#ifndef KOBOINPUTMANAGER_H
#define KOBOINPUTMANAGER_H

#include <QList>
#include <QObject>
#include <QStringList>

#include "fbink.h"
#include "kobodevicedescriptor.h"

class KoboFbScreen;
class KoboKeyHandler;
class KoboTouchHandlerThread;

// How the touch panel signals contacts and lifts, c.f. KOReader's input.lua
enum class KoboTouchProtocol
{
    Std,      // multitouch protocol B: slots, lift = ABS_MT_TRACKING_ID -1 (sunxi, MTK, Libra 2, ...)
    Snow,     // protocol A framing, tracking id = slot, never -1; lift = pressure 0, missing contact or
              // BTN_TOUCH 0 (Aura H2O², Clara HD, Forma, Libra H2O, Clara BW/Colour)
    Phoenix,  // protocol A framing, tracking id = slot; lift = ABS_MT_TOUCH_MAJOR 0 or missing contact
              // (Aura, Aura H2O, Aura SE, Glo HD, Touch 2.0, Aura One, Nia)
    Legacy    // single touch: ABS_X/ABS_Y, lift = ABS_PRESSURE 0 / BTN_TOUCH 0 (Touch A/B/C, Mini, Glo, Aura HD)
};

struct KoboTouchSettings
{
    KoboTouchProtocol protocol = KoboTouchProtocol::Std;
    bool swapXY = false;
    bool mirrorX = false;
    bool mirrorY = false;
    // Canonical rotation of the screen (0 = upright, 1 = CW, 2 = upside down, 3 = CCW)
    int rotation = 0;
    // Size of the screen in the canonical (upright) rotation, which is what the swap/mirror quirks refer to
    int uprightWidth = 0;
    int uprightHeight = 0;
    // Overrides for the coordinate range reported by the touch panel, 0 to use the panel's own
    int rangeMaxX = 0;
    int rangeMaxY = 0;
    bool debug = false;
};

// Finds the touchscreen and button input devices and creates the handlers for them.
//
// Plugin parameters (QT_QPA_PLATFORM=kobo:param1:param2...):
//   touchscreen_device=/dev/input/eventX     use this touchscreen instead of auto detection
//   touchscreen_protocol=std|snow|phoenix|legacy  override the protocol picked for the device
//   button_devices=/dev/input/eventX,...     use these button devices instead of auto detection
//   touchscreen_swap_xy=0|1                  override the panel quirks from FBInk's device database
//   touchscreen_invert_x=0|1
//   touchscreen_invert_y=0|1
//   touchscreen_max_range_x=N                override the panel's coordinate range
//   touchscreen_max_range_y=N
//   nograb                                   don't grab the button devices
class KoboInputManager : public QObject
{
    Q_OBJECT

public:
    KoboInputManager(KoboFbScreen *screen, KoboDeviceDescriptor *device, const QStringList &params, bool debug,
                     QObject *parent = nullptr);
    ~KoboInputManager();

private:
    struct InputDevice
    {
        QString path;
        int fd = -1;  // already opened by the scan, or -1
    };

    void discoverDevices(QList<InputDevice> &touchDevices, QList<InputDevice> &buttonDevices);
    static KoboTouchProtocol protocolForDevice(const FBInkState *state);
    void updateDeviceCount();

    QList<KoboTouchHandlerThread *> m_touchHandlers;
    QList<KoboKeyHandler *> m_keyHandlers;
    bool m_debug;
};

#endif  // KOBOINPUTMANAGER_H
