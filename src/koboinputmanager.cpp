#include "koboinputmanager.h"

#include <QtGui/private/qguiapplication_p.h>
#include <QtGui/private/qinputdevicemanager_p_p.h>

#include <QDebug>
#include <QFile>
#include <QRegularExpression>
#include <cstdlib>
#include <unistd.h>

#include "fbink.h"
#include "kobofbscreen.h"
#include "kobokeyhandler.h"
#include "kobotouchhandler.h"

static const INPUT_DEVICE_TYPE_T TouchTypes = INPUT_TOUCHSCREEN | INPUT_TABLET;
static const INPUT_DEVICE_TYPE_T ButtonTypes = INPUT_POWER_BUTTON | INPUT_SLEEP_COVER | INPUT_PAGINATION_BUTTONS |
                                               INPUT_HOME_BUTTON | INPUT_LIGHT_BUTTON | INPUT_MENU_BUTTON;

KoboInputManager::KoboInputManager(KoboFbScreen *screen, KoboDeviceDescriptor *device, const QStringList &params,
                                   bool debug, QObject *parent)
    : QObject(parent), m_debug(debug)
{
    FBInkState *state = screen->getFBInkState();

    // Touch panel quirks, as known by FBInk's device database
    KoboTouchSettings settings;
    settings.debug = debug;
    settings.swapXY = state->touch_swap_axes;
    settings.mirrorX = state->touch_mirror_x;
    settings.mirrorY = state->touch_mirror_y;

    // The quirks describe the panel relative to the upright (canonical portrait) orientation
    settings.rotation = fbink_rota_native_to_canonical(state->current_rota);
    bool rotatedByOdd = settings.rotation & 1;
    settings.uprightWidth = rotatedByOdd ? state->screen_height : state->screen_width;
    settings.uprightHeight = rotatedByOdd ? state->screen_width : state->screen_height;

    settings.protocol = protocolForDevice(state);

    QList<InputDevice> touchDevices, buttonDevices;
    bool grabButtons = true;

    QRegularExpression touchDevRx("^touchscreen_device=(.+)$");
    QRegularExpression touchProtocolRx("^touchscreen_protocol=(std|snow|phoenix|legacy)$");
    QRegularExpression buttonDevRx("^button_devices=(.+)$");
    QRegularExpression touchSwapXYRx("^touchscreen_swap_xy=(\\d)$");
    QRegularExpression touchInvXRx("^touchscreen_invert_x=(\\d)$");
    QRegularExpression touchInvYRx("^touchscreen_invert_y=(\\d)$");
    QRegularExpression touchRangeXRx("^touchscreen_max_range_x=(\\d+)$");
    QRegularExpression touchRangeYRx("^touchscreen_max_range_y=(\\d+)$");

    for (const QString &arg : params)
    {
        QRegularExpressionMatch match;
        if ((match = touchDevRx.match(arg)).hasMatch())
            touchDevices << InputDevice{match.captured(1)};
        else if ((match = buttonDevRx.match(arg)).hasMatch())
            for (const QString &path : match.captured(1).split(',', Qt::SkipEmptyParts))
                buttonDevices << InputDevice{path};
        else if ((match = touchProtocolRx.match(arg)).hasMatch())
        {
            const QString p = match.captured(1);
            settings.protocol = p == "snow"      ? KoboTouchProtocol::Snow
                                : p == "phoenix" ? KoboTouchProtocol::Phoenix
                                : p == "legacy"  ? KoboTouchProtocol::Legacy
                                                 : KoboTouchProtocol::Std;
        }
        else if ((match = touchSwapXYRx.match(arg)).hasMatch())
            settings.swapXY = match.captured(1).toInt() > 0;
        else if ((match = touchInvXRx.match(arg)).hasMatch())
            settings.mirrorX = match.captured(1).toInt() > 0;
        else if ((match = touchInvYRx.match(arg)).hasMatch())
            settings.mirrorY = match.captured(1).toInt() > 0;
        else if ((match = touchRangeXRx.match(arg)).hasMatch())
            settings.rangeMaxX = match.captured(1).toInt();
        else if ((match = touchRangeYRx.match(arg)).hasMatch())
            settings.rangeMaxY = match.captured(1).toInt();
        else if (arg == QLatin1String("nograb"))
            grabButtons = false;
    }

    // Only probe for what wasn't given explicitly
    QList<InputDevice> foundTouch, foundButtons;
    if (touchDevices.isEmpty() || buttonDevices.isEmpty())
        discoverDevices(foundTouch, foundButtons);
    auto closeAll = [](const QList<InputDevice> &devices) {
        for (const InputDevice &dev : devices)
            if (dev.fd >= 0)
                ::close(dev.fd);
    };
    if (touchDevices.isEmpty())
        touchDevices = foundTouch;
    else
        closeAll(foundTouch);
    if (buttonDevices.isEmpty())
        buttonDevices = foundButtons;
    else
        closeAll(foundButtons);

    // Fall back to the classic paths if probing didn't work out
    if (touchDevices.isEmpty() && QFile::exists(device->touchDev))
        touchDevices << InputDevice{device->touchDev};
    if (buttonDevices.isEmpty())
    {
        if (QFile::exists(device->ntxDev))
            buttonDevices << InputDevice{device->ntxDev};
        if (QFile::exists(device->powerDev))
            buttonDevices << InputDevice{device->powerDev};
    }

    device->touchscreenSettings.swapXY = settings.swapXY;
    device->touchscreenSettings.invertX = settings.mirrorX;
    device->touchscreenSettings.invertY = settings.mirrorY;

    if (debug)
    {
        for (const InputDevice &dev : std::as_const(touchDevices))
            qDebug() << "koboinput: touch device:" << dev.path;
        for (const InputDevice &dev : std::as_const(buttonDevices))
            qDebug() << "koboinput: button device:" << dev.path;
        qDebug() << "koboinput: touch protocol:" << int(settings.protocol) << "swap xy:" << settings.swapXY
                 << "mirror x:" << settings.mirrorX << "mirror y:" << settings.mirrorY
                 << "rotation:" << settings.rotation << "upright size:" << settings.uprightWidth
                 << settings.uprightHeight;
    }

    if (touchDevices.isEmpty())
        qWarning("koboinput: No touchscreen found");

    for (const InputDevice &dev : std::as_const(touchDevices))
    {
        auto handler = new KoboTouchHandlerThread(dev.path, dev.fd, settings);
        if (handler->isValid())
            m_touchHandlers << handler;
        else
            delete handler;
    }

    for (const InputDevice &dev : std::as_const(buttonDevices))
    {
        auto handler = new KoboKeyHandler(dev.path, dev.fd, grabButtons, debug, this);
        if (handler->isValid())
            m_keyHandlers << handler;
        else
            delete handler;
    }

    updateDeviceCount();
}

KoboInputManager::~KoboInputManager()
{
    qDeleteAll(m_touchHandlers);
    m_touchHandlers.clear();
}

void KoboInputManager::discoverDevices(QList<InputDevice> &touchDevices, QList<InputDevice> &buttonDevices)
{
    size_t count = 0;
    // Keep the fds of the matched devices: closing and reopening toggles zForce IR panels off and on.
    // Keyboards are excluded like KOReader does, so a USB keyboard's Home key isn't mistaken for the Kobo one.
    INPUT_SETTINGS_TYPE_T scanSettings = m_debug ? 0 : NO_RECAP;
    FBInkInputDevice *devices = fbink_input_scan(TouchTypes | ButtonTypes, INPUT_KEYBOARD, scanSettings, &count);
    if (!devices)
    {
        qWarning("koboinput: Input device scan failed");
        return;
    }

    for (size_t i = 0; i < count; i++)
    {
        const FBInkInputDevice &dev = devices[i];
        if (!dev.matched)
            continue;

        if (m_debug)
            qDebug("koboinput: found %s (%s), type %#x", dev.path, dev.name, dev.type);

        // A touchscreen can also report keys (e.g. the stylus buttons), the touch handler takes care of those
        InputDevice input{QString::fromLocal8Bit(dev.path), dev.fd};
        if (dev.type & TouchTypes)
            touchDevices << input;
        else if (dev.type & ButtonTypes)
            buttonDevices << input;
        else if (dev.fd >= 0)
            ::close(dev.fd);
    }

    free(devices);
}

// c.f. the touch_*_protocol flags in KOReader's frontend/device/kobo/device.lua
KoboTouchProtocol KoboInputManager::protocolForDevice(const FBInkState *state)
{
    if (state->is_kobo_non_mt)
        return KoboTouchProtocol::Legacy;

    switch (state->device_id)
    {
        case DEVICE_KOBO_AURA:
        case DEVICE_KOBO_AURA_H2O:
        case DEVICE_KOBO_AURA_SE:
        case DEVICE_TOLINO_VISION:
        case DEVICE_KOBO_AURA_SE_R2:
        case DEVICE_KOBO_GLO_HD:
        case DEVICE_TOLINO_SHINE_2HD:
        case DEVICE_KOBO_TOUCH_2:
        case DEVICE_KOBO_AURA_ONE:
        case DEVICE_KOBO_AURA_ONE_LE:
        case DEVICE_KOBO_NIA:
            return KoboTouchProtocol::Phoenix;

        case DEVICE_KOBO_AURA_H2O_2:
        case DEVICE_KOBO_AURA_H2O_2_R2:
        case DEVICE_KOBO_CLARA_HD:
        case DEVICE_TOLINO_SHINE_3:
        case DEVICE_KOBO_FORMA:
        case DEVICE_KOBO_FORMA_32GB:
        case DEVICE_TOLINO_EPOS_2:
        case DEVICE_KOBO_LIBRA_H2O:
        case DEVICE_TOLINO_VISION_5:
        case DEVICE_KOBO_CLARA_BW:
        case DEVICE_TOLINO_SHINE_BW:
        case DEVICE_KOBO_CLARA_BW_TPV:
        case DEVICE_KOBO_CLARA_COLOUR:
        case DEVICE_TOLINO_SHINE_COLOR:
            return KoboTouchProtocol::Snow;

        // Elipsa, Sage, Libra 2, Clara 2E, Elipsa 2E, Libra Colour, and anything newer.
        // The handler switches to Snow by itself if the panel turns out to use protocol A framing.
        default:
            return KoboTouchProtocol::Std;
    }
}

void KoboInputManager::updateDeviceCount()
{
    QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())
        ->setDeviceCount(QInputDeviceManager::DeviceTypeTouch, m_touchHandlers.size());
}
