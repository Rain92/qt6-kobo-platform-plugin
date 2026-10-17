#include "kobotouchhandler.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QDebug>
#include <QSocketNotifier>
#include <cmath>
#include <cstring>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QPointingDevice>
#include <QtGui/private/qpointingdevice_p.h>
#else
#include <QtGui/QTouchDevice>
#endif

#include "kobokeyhandler.h"

// The Kobo sysroot ships 2.6.35 kernel headers
#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif
#ifndef SYN_DROPPED
#define SYN_DROPPED 3
#endif

#define LONG_BITS (sizeof(long) << 3)
#define NUM_LONGS(bits) (((bits) + LONG_BITS - 1) / LONG_BITS)

static inline bool testBit(long bit, const long *array)
{
    return (array[bit / LONG_BITS] >> bit % LONG_BITS) & 1;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
using TouchPointState = QEventPoint::State;
static constexpr TouchPointState PointPressed = QEventPoint::State::Pressed;
static constexpr TouchPointState PointMoved = QEventPoint::State::Updated;
static constexpr TouchPointState PointStationary = QEventPoint::State::Stationary;
static constexpr TouchPointState PointReleased = QEventPoint::State::Released;
#else
using TouchPointState = Qt::TouchPointState;
static constexpr TouchPointState PointPressed = Qt::TouchPointPressed;
static constexpr TouchPointState PointMoved = Qt::TouchPointMoved;
static constexpr TouchPointState PointStationary = Qt::TouchPointStationary;
static constexpr TouchPointState PointReleased = Qt::TouchPointReleased;
#endif

// Protocol A panels use the tracking id as slot number, so give them enough slots
static constexpr int MaxSlots = 16;

static const char *protocolName(KoboTouchProtocol protocol)
{
    switch (protocol)
    {
        case KoboTouchProtocol::Std:
            return "std";
        case KoboTouchProtocol::Snow:
            return "snow";
        case KoboTouchProtocol::Phoenix:
            return "phoenix";
        case KoboTouchProtocol::Legacy:
            return "legacy";
    }
    return "?";
}

KoboTouchHandler::KoboTouchHandler(const QString &devicePath, int fd, const KoboTouchSettings &settings,
                                   QObject *parent)
    : QObject(parent),
      m_devicePath(devicePath),
      m_settings(settings),
      m_protocol(settings.protocol),
      m_fd(fd),
      m_notifier(nullptr),
      m_hasMtSlots(false),
      m_hasMtPosition(false),
      m_hasMtPressure(false),
      m_hasStPressure(false),
      m_hasPen(false),
      m_maxSlots(1),
      m_rawMinX(0),
      m_rawMaxX(0),
      m_rawMinY(0),
      m_rawMaxY(0),
      m_pressureMin(0),
      m_pressureMax(0),
      m_currentSlot(0),
      m_frameHasMtData(false),
      m_sawLiftTrackingId(false),
      m_dropping(false),
      m_nextQtId(1)
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
      ,
      m_devices{nullptr, nullptr}
#else
      ,
      m_device(nullptr)
#endif
{
    // -2: delegated to by the replay constructor, which sets things up itself
    if (fd == -2)
    {
        m_fd = -1;
        return;
    }

    if (m_fd < 0)
        m_fd = ::open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
    {
        qWarning("kobotouch: Cannot open input device %s: %s", qPrintable(devicePath), strerror(errno));
        return;
    }

    probeDevice();
    registerDevices();
}

KoboTouchHandler::~KoboTouchHandler()
{
    // NOTE: Closing toggles zForce IR panels off, so the fd stays open for the whole lifetime.
    if (m_fd >= 0)
        ::close(m_fd);

    unregisterDevices();
}

KoboTouchHandler::KoboTouchHandler(int fd, const KoboTouchCapabilities &caps, const KoboTouchSettings &settings,
                                   QObject *parent)
    : KoboTouchHandler(QStringLiteral("replay"), -2, settings, parent)
{
    // Only used by the event replay tests: the fd is a pipe, so the capabilities can't be queried
    m_fd = fd;
    applyCapabilities(caps);
    registerDevices();
}

KoboTouchCapabilities KoboTouchHandler::queryCapabilities(int fd)
{
    KoboTouchCapabilities caps;

    char name[256] = {0};
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), name) >= 0)
        caps.name = QString::fromLocal8Bit(name);

    long absbits[NUM_LONGS(ABS_CNT)] = {0};
    long keybits[NUM_LONGS(KEY_CNT)] = {0};
    ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);

    caps.hasMtSlots = testBit(ABS_MT_SLOT, absbits);
    caps.hasMtPosition = testBit(ABS_MT_POSITION_X, absbits);
    caps.hasPen = testBit(BTN_TOOL_PEN, keybits) || testBit(BTN_STYLUS, keybits);

    auto query = [&](int axis, int &min, int &max) {
        input_absinfo absInfo;
        if (testBit(axis, absbits) && ioctl(fd, EVIOCGABS(axis), &absInfo) >= 0)
        {
            min = absInfo.minimum;
            max = absInfo.maximum;
            return true;
        }
        return false;
    };

    int min = 0, max = -1;
    if (caps.hasMtSlots && query(ABS_MT_SLOT, min, max))
        caps.maxSlot = max;
    if (query(ABS_MT_TOOL_TYPE, min, max) && max >= MT_TOOL_PEN)
        caps.hasPen = true;

    query(ABS_MT_POSITION_X, caps.mtMinX, caps.mtMaxX);
    query(ABS_MT_POSITION_Y, caps.mtMinY, caps.mtMaxY);
    query(ABS_X, caps.stMinX, caps.stMaxX);
    query(ABS_Y, caps.stMinY, caps.stMaxY);
    query(ABS_MT_PRESSURE, caps.mtPressureMin, caps.mtPressureMax);
    query(ABS_PRESSURE, caps.stPressureMin, caps.stPressureMax);

    return caps;
}

void KoboTouchHandler::probeDevice()
{
    applyCapabilities(queryCapabilities(m_fd));

    if (ioctl(m_fd, EVIOCGRAB, (void *)1) == 0)
        ioctl(m_fd, EVIOCGRAB, (void *)0);
    else
        qWarning("kobotouch: %s is grabbed by another process, no events will be read.", qPrintable(m_devicePath));
}

void KoboTouchHandler::applyCapabilities(const KoboTouchCapabilities &caps)
{
    m_name = caps.name;
    m_hasMtSlots = caps.hasMtSlots;
    m_hasMtPosition = caps.hasMtPosition;
    m_hasPen = caps.hasPen;

    // Without multitouch there's only one way to decode it
    if (!m_hasMtPosition && m_protocol != KoboTouchProtocol::Legacy)
        switchProtocol(KoboTouchProtocol::Legacy, "panel has no multitouch axes");

    bool legacy = m_protocol == KoboTouchProtocol::Legacy;
    if (legacy)
        m_maxSlots = 1;
    else if (m_protocol == KoboTouchProtocol::Std && m_hasMtSlots && caps.maxSlot >= 0)
        m_maxSlots = qBound(1, caps.maxSlot + 1, MaxSlots);
    else
        m_maxSlots = MaxSlots;

    m_rawMinX = legacy ? caps.stMinX : caps.mtMinX;
    m_rawMaxX = legacy ? caps.stMaxX : caps.mtMaxX;
    m_rawMinY = legacy ? caps.stMinY : caps.mtMinY;
    m_rawMaxY = legacy ? caps.stMaxY : caps.mtMaxY;

    if (legacy && caps.stPressureMax > caps.stPressureMin)
    {
        m_hasStPressure = true;
        m_pressureMin = caps.stPressureMin;
        m_pressureMax = caps.stPressureMax;
    }
    else if (!legacy && caps.mtPressureMax > caps.mtPressureMin)
    {
        m_hasMtPressure = true;
        m_pressureMin = caps.mtPressureMin;
        m_pressureMax = caps.mtPressureMax;
    }

    // Always room for 16 contacts on multitouch panels, in case the protocol gets switched at runtime
    m_contacts.resize(legacy ? 1 : MaxSlots);

    // The panel ranges are only used for diagnostics: like KOReader and Plato we treat the raw coordinates as
    // screen pixels, because some drivers report bogus ranges. touchscreen_max_range_x/y can override that.
    int expectedX = (m_settings.swapXY ? m_settings.uprightHeight : m_settings.uprightWidth) - 1;
    int expectedY = (m_settings.swapXY ? m_settings.uprightWidth : m_settings.uprightHeight) - 1;
    bool rangeMismatch = m_settings.rangeMaxX == 0 && m_settings.rangeMaxY == 0 &&
                         (qAbs(m_rawMaxX - expectedX) > 2 || qAbs(m_rawMaxY - expectedY) > 2);

    if (m_settings.debug || rangeMismatch)
        qDebug().nospace() << "kobotouch: " << m_devicePath << " \"" << m_name
                           << "\" protocol: " << protocolName(m_protocol) << " slots: " << m_maxSlots
                           << " pen: " << m_hasPen << " x: " << m_rawMinX << ".." << m_rawMaxX << " y: " << m_rawMinY
                           << ".." << m_rawMaxY << " (screen expects " << expectedX << ", " << expectedY << ")"
                           << " pressure: " << m_pressureMin << ".." << m_pressureMax;
    if (rangeMismatch)
        qDebug("kobotouch: if touches end up in the wrong place, try touchscreen_max_range_x/y=<panel max>");
}

void KoboTouchHandler::switchProtocol(KoboTouchProtocol protocol, const char *reason)
{
    if (m_protocol == protocol)
        return;

    qWarning("kobotouch: switching from %s to %s protocol: %s", protocolName(m_protocol), protocolName(protocol),
             reason);
    m_protocol = protocol;
}

QRectF KoboTouchHandler::screenGeometry() const
{
    bool odd = m_settings.rotation & 1;
    return QRectF(0, 0, odd ? m_settings.uprightHeight : m_settings.uprightWidth,
                  odd ? m_settings.uprightWidth : m_settings.uprightHeight);
}

void KoboTouchHandler::registerDevices()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    static int deviceId = 1;
    QPointingDevice::Capabilities caps = QPointingDevice::Capability::Position | QPointingDevice::Capability::Area;
    if (m_hasMtPressure || m_hasStPressure)
        caps |= QPointingDevice::Capability::Pressure;

    // Separate devices, so apps can tell the stylus from fingers via QPointingDevice::pointerType()
    for (int tool = 0; tool < ToolCount; tool++)
    {
        if (tool == ToolPen && !m_hasPen)
            continue;

        auto pointerType = tool == ToolPen ? QPointingDevice::PointerType::Pen : QPointingDevice::PointerType::Finger;
        QString name = m_name + (tool == ToolPen ? QStringLiteral(" (Pen)") : QString());
        auto device = new QPointingDevice(name, deviceId++, QInputDevice::DeviceType::TouchScreen, pointerType, caps,
                                          m_maxSlots, 0);
        QPointingDevicePrivate::get(device)->setAvailableVirtualGeometry(screenGeometry().toRect());
        QWindowSystemInterface::registerInputDevice(device);
        m_devices[tool] = device;
    }
#else
    m_device = new QTouchDevice;
    m_device->setName(m_name);
    m_device->setType(QTouchDevice::TouchScreen);
    QTouchDevice::Capabilities caps = QTouchDevice::Position | QTouchDevice::Area;
    if (m_hasMtPressure || m_hasStPressure)
        caps |= QTouchDevice::Pressure;
    m_device->setCapabilities(caps);
    m_device->setMaximumTouchPoints(m_maxSlots);
    QWindowSystemInterface::registerTouchDevice(m_device);
#endif
}

void KoboTouchHandler::unregisterDevices()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Destroyed in the gui thread, after the reader thread finished
    for (auto &device : m_devices)
    {
        delete device;
        device = nullptr;
    }
#else
    // At app exit the cleanup may have already been done, avoid double delete by checking the list first.
    if (m_device && QWindowSystemInterface::isTouchDeviceRegistered(m_device))
    {
        QWindowSystemInterface::unregisterTouchDevice(m_device);
        delete m_device;
    }
    m_device = nullptr;
#endif
}

void KoboTouchHandler::startReading()
{
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &KoboTouchHandler::readData);
}

void KoboTouchHandler::stopReading()
{
    delete m_notifier;
    m_notifier = nullptr;
}

void KoboTouchHandler::readData()
{
    input_event buffer[64];

    for (;;)
    {
        ssize_t n = ::read(m_fd, buffer, sizeof(buffer));
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
            {
                qWarning("kobotouch: Could not read from %s: %s", qPrintable(m_devicePath), strerror(errno));
                if (errno == ENODEV)
                    m_notifier->setEnabled(false);
            }
            return;
        }
        if (n == 0)
            return;

        // evdev only ever returns whole events
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); i++)
            processEvent(buffer[i]);
    }
}

void KoboTouchHandler::processEvent(const input_event &ev)
{
    if (m_settings.debug)
        qDebug("kobotouch: type %d code %d value %d", ev.type, ev.code, ev.value);

    if (ev.type == EV_SYN && ev.code == SYN_DROPPED)
    {
        // The kernel buffer overflowed: drop everything until the next SYN_REPORT and start over
        m_dropping = true;
        return;
    }

    if (m_dropping)
    {
        if (ev.type == EV_SYN && ev.code == SYN_REPORT)
        {
            m_dropping = false;
            releaseAll();
            commitFrame();
        }
        return;
    }

    switch (ev.type)
    {
        case EV_ABS:
            processAbs(ev);
            break;

        case EV_KEY:
            processKey(ev);
            break;

        case EV_SYN:
            if (ev.code == SYN_MT_REPORT)
            {
                m_frameHasMtData = true;
                // KOReader and Plato disagree on whether some newer panels (Clara 2E, Libra Colour)
                // use protocol A framing, so follow what the panel actually does.
                if (m_protocol == KoboTouchProtocol::Std && !m_sawLiftTrackingId)
                {
                    switchProtocol(KoboTouchProtocol::Snow, "panel uses SYN_MT_REPORT framing");

                    // Move the contact decoded so far into the slot snow uses for its tracking id
                    Contact &c = m_contacts[m_currentSlot];
                    int slot = c.trackingId >= 0 ? c.trackingId % m_contacts.size() : m_currentSlot;
                    if (slot != m_currentSlot && c.down && !c.reportedDown)
                    {
                        m_contacts[slot] = c;
                        c = Contact();
                        m_currentSlot = slot;
                    }
                }
            }
            else if (ev.code == SYN_REPORT)
            {
                // Protocol A frames list every contact that is still down
                bool protocolA = m_protocol == KoboTouchProtocol::Snow || m_protocol == KoboTouchProtocol::Phoenix;
                if (protocolA && m_frameHasMtData)
                    for (Contact &c : m_contacts)
                        if (!c.seenInFrame)
                            c.down = false;

                commitFrame();

                for (Contact &c : m_contacts)
                    c.seenInFrame = false;
                m_frameHasMtData = false;
            }
            break;

        default:
            break;
    }
}

void KoboTouchHandler::selectSlotForTrackingId(int trackingId)
{
    m_currentSlot = trackingId % m_contacts.size();
}

void KoboTouchHandler::processAbs(const input_event &ev)
{
    switch (ev.code)
    {
        case ABS_MT_SLOT:
            if (m_protocol == KoboTouchProtocol::Std)
                m_currentSlot = qBound(0, ev.value, m_contacts.size() - 1);
            return;

        case ABS_MT_TRACKING_ID:
        {
            m_frameHasMtData = true;
            if (ev.value < 0)
            {
                m_sawLiftTrackingId = true;
                // Newer revisions of the snow panels are sane, KOReader does the same
                if (m_protocol == KoboTouchProtocol::Snow)
                    switchProtocol(KoboTouchProtocol::Std, "panel sends ABS_MT_TRACKING_ID -1");
                m_contacts[m_currentSlot].down = false;
                return;
            }

            if (m_protocol == KoboTouchProtocol::Std)
            {
                // sunxi panels can leave out ABS_MT_SLOT once a single contact is left, so the data may not be
                // for the last selected slot. Tracking ids are unique among the active contacts, follow them.
                if (!m_contacts[m_currentSlot].down || m_contacts[m_currentSlot].trackingId != ev.value)
                {
                    for (int i = 0; i < m_contacts.size(); i++)
                    {
                        if (m_contacts[i].down && m_contacts[i].trackingId == ev.value)
                        {
                            m_currentSlot = i;
                            break;
                        }
                    }
                }

                Contact &c = m_contacts[m_currentSlot];
                // sunxi panels resend the same id with every frame, only a different one is a new contact
                if (c.down && c.trackingId >= 0 && c.trackingId != ev.value)
                    c.restart = true;
                c.trackingId = ev.value;
                c.down = true;
                // Only matters if the panel turns out to use protocol A framing after all
                c.seenInFrame = true;
            }
            else
            {
                // Snow & Phoenix: the tracking id is the slot number, and starts every contact block
                selectSlotForTrackingId(ev.value);
                Contact &c = m_contacts[m_currentSlot];
                c.trackingId = ev.value;
                c.down = true;
                c.seenInFrame = true;
            }
            return;
        }

        default:
            break;
    }

    Contact &c = m_contacts[m_currentSlot];

    switch (ev.code)
    {
        case ABS_MT_POSITION_X:
            if (m_protocol != KoboTouchProtocol::Legacy)
            {
                c.x = ev.value;
                c.hasPosition = true;
                c.moved = true;
            }
            break;

        case ABS_MT_POSITION_Y:
            if (m_protocol != KoboTouchProtocol::Legacy)
            {
                c.y = ev.value;
                c.hasPosition = true;
                c.moved = true;
            }
            break;

        // Multitouch panels also emulate these for some contact, with no slot info: single touch only
        case ABS_X:
            if (m_protocol == KoboTouchProtocol::Legacy)
            {
                c.x = ev.value;
                c.hasPosition = true;
                c.moved = true;
            }
            break;

        case ABS_Y:
            if (m_protocol == KoboTouchProtocol::Legacy)
            {
                c.y = ev.value;
                c.hasPosition = true;
                c.moved = true;
            }
            break;

        case ABS_PRESSURE:
            if (m_protocol == KoboTouchProtocol::Legacy)
            {
                c.pressure = ev.value;
                c.down = ev.value > 0;
            }
            break;

        case ABS_MT_PRESSURE:
            c.pressure = ev.value;
            // Snow lifts are a contact block with pressure 0 (touch major is always 0 there)
            if (m_protocol == KoboTouchProtocol::Snow && ev.value == 0)
                c.down = false;
            break;

        case ABS_MT_TOUCH_MAJOR:
            // Phoenix lifts are a contact block with touch major 0
            if (m_protocol == KoboTouchProtocol::Phoenix && ev.value == 0)
                c.down = false;
            break;

        case ABS_MT_TOOL_TYPE:
            // 1 is MT_TOOL_PEN, the Elan panels also report 2 for the eraser and 3 for the highlighter
            c.tool = (ev.value >= MT_TOOL_PEN && ev.value <= 3) ? ToolPen : ToolFinger;
            break;

        default:
            break;
    }
}

void KoboTouchHandler::releaseAll()
{
    for (Contact &c : m_contacts)
        c.down = false;
}

void KoboTouchHandler::processKey(const input_event &ev)
{
    switch (ev.code)
    {
        case BTN_TOUCH:
            switch (m_protocol)
            {
                case KoboTouchProtocol::Legacy:
                    if (ev.value == 0)
                        m_contacts[0].down = false;
                    else if (!m_hasStPressure)
                        m_contacts[0].down = true;
                    break;
                case KoboTouchProtocol::Std:
                case KoboTouchProtocol::Snow:
                    // Only sent once the last contact is lifted, and snow panels never send a per contact lift
                    if (ev.value == 0)
                        releaseAll();
                    break;
                case KoboTouchProtocol::Phoenix:
                    // Not reliable on these, c.f. KOReader
                    break;
            }
            break;

        // The tool is reported per contact through ABS_MT_TOOL_TYPE
        case BTN_TOOL_FINGER:
        case BTN_TOOL_PEN:
        case BTN_TOOL_RUBBER:
            break;

        default:
            // Everything else (e.g. the stylus buttons) goes through the hardware key mapping
            KoboKeyHandler::processKeyEvent(ev, m_settings.debug);
            break;
    }
}

// A hovering stylus still reports a contact, but with pressure 0
bool KoboTouchHandler::isEffectivelyDown(const Contact &c) const
{
    return c.down && !(c.tool == ToolPen && m_hasMtPressure && c.pressure == 0);
}

QPointF KoboTouchHandler::transform(int rawX, int rawY) const
{
    // Raw coordinates are screen pixels in the panel's own orientation, see probeDevice()
    qreal x = rawX, y = rawY;
    int rangeX = m_settings.rangeMaxX, rangeY = m_settings.rangeMaxY;

    // 1. swap
    if (m_settings.swapXY)
    {
        std::swap(x, y);
        std::swap(rangeX, rangeY);
    }

    qreal w = m_settings.uprightWidth - 1;
    qreal h = m_settings.uprightHeight - 1;
    if (rangeX > 0)
        x = x * w / rangeX;
    if (rangeY > 0)
        y = y * h / rangeY;

    // 2. mirror, against the upright (portrait) screen size
    if (m_settings.mirrorX)
        x = w - x;
    if (m_settings.mirrorY)
        y = h - y;

    qreal nx = qBound<qreal>(0, x / w, 1);
    qreal ny = qBound<qreal>(0, y / h, 1);

    // 3. rotate into the current screen orientation (the plugin keeps the screen upright, so normally a no-op)
    switch (m_settings.rotation)
    {
        case 1:  // CW
            return QPointF(1 - ny, nx);
        case 2:  // UD
            return QPointF(1 - nx, 1 - ny);
        case 3:  // CCW
            return QPointF(ny, 1 - nx);
        default:
            return QPointF(nx, ny);
    }
}

void KoboTouchHandler::commitFrame()
{
    const QRectF screen = screenGeometry();
    QList<QWindowSystemInterface::TouchPoint> points[ToolCount];
    bool changed[ToolCount] = {false, false};

    auto addPoint = [&](const Contact &c, int qtId, const QPointF &normalPos, TouchPointState state) {
        QWindowSystemInterface::TouchPoint tp;
        tp.id = qtId;
        tp.state = state;
        tp.normalPosition = normalPos;
        // Qt wants the area in screen coordinates
        QPointF screenPos(screen.left() + normalPos.x() * (screen.width() - 1),
                          screen.top() + normalPos.y() * (screen.height() - 1));
        tp.area = QRectF(0, 0, 8, 8);
        tp.area.moveCenter(screenPos);
        tp.rawPositions.append(QPointF(c.x, c.y));
        if (state == PointReleased)
            tp.pressure = 0;
        else if (m_pressureMax > m_pressureMin && c.pressure > 0)
            tp.pressure = qBound<qreal>(0, qreal(c.pressure - m_pressureMin) / (m_pressureMax - m_pressureMin), 1);
        else
            tp.pressure = 1;

        int tool = (c.tool == ToolPen && m_hasPen) ? ToolPen : ToolFinger;
        points[tool].append(tp);
        if (state != PointStationary)
            changed[tool] = true;
    };

    for (Contact &c : m_contacts)
    {
        bool down = isEffectivelyDown(c);

        // The slot got a new contact without the old one being lifted
        if (c.restart && c.reportedDown)
        {
            addPoint(c, c.qtId, c.lastPos, PointReleased);
            c.reportedDown = false;
        }
        c.restart = false;

        if (down && !c.reportedDown)
        {
            // Don't report a contact before we know where it is.
            // NOTE: hasPosition is never reset, the kernel doesn't resend unchanged coordinates for a slot.
            if (c.hasPosition)
            {
                c.qtId = m_nextQtId++;
                c.lastPos = transform(c.x, c.y);
                c.reportedDown = true;
                addPoint(c, c.qtId, c.lastPos, PointPressed);
            }
        }
        else if (down && c.reportedDown)
        {
            QPointF pos = transform(c.x, c.y);
            bool moved = c.moved && pos != c.lastPos;
            c.lastPos = pos;
            addPoint(c, c.qtId, pos, moved ? PointMoved : PointStationary);
        }
        else if (!down && c.reportedDown)
        {
            // Use the last known position: lift frames can carry odd coordinates (e.g. on the Touch B)
            addPoint(c, c.qtId, c.lastPos, PointReleased);
            c.reportedDown = false;
        }

        c.moved = false;
    }

    for (int tool = 0; tool < ToolCount; tool++)
    {
        if (!changed[tool])
            continue;

        if (m_settings.debug)
            for (const auto &tp : std::as_const(points[tool]))
                qDebug() << "kobotouch: point" << tp.id << tp.state << tp.area.center() << "raw" << tp.rawPositions;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        QWindowSystemInterface::handleTouchEvent(nullptr, m_devices[tool], points[tool]);
#else
        QWindowSystemInterface::handleTouchEvent(nullptr, m_device, points[tool]);
#endif
    }
}

KoboTouchHandlerThread::KoboTouchHandlerThread(const QString &devicePath, int fd, const KoboTouchSettings &settings,
                                               QObject *parent)
    : QDaemonThread(parent), m_handler(new KoboTouchHandler(devicePath, fd, settings))
{
    if (!m_handler->isValid())
        return;

    // The devices are registered in the gui thread, the handler itself reads events in this thread
    m_handler->moveToThread(this);
    start();
}

KoboTouchHandlerThread::~KoboTouchHandlerThread()
{
    quit();
    wait();
    delete m_handler;
}

void KoboTouchHandlerThread::run()
{
    m_handler->startReading();
    exec();
    m_handler->stopReading();
}
