#include "kobokeyhandler.h"

#include <errno.h>
#include <fcntl.h>
#include <qpa/qwindowsysteminterface.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <QDebug>
#include <QSocketNotifier>

#include "einkenums.h"

#define EVENT_RELEASE 0
#define EVENT_PRESS 1
#define EVENT_REPEAT 2

KoboKeyHandler::KoboKeyHandler(const QString &devicePath, int fd, bool grab, bool debug, QObject *parent)
    : QObject(parent), m_devicePath(devicePath), m_fd(fd), m_notifier(nullptr), m_grabbed(false), m_debug(debug)
{
    if (m_fd < 0)
        m_fd = ::open(devicePath.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
    {
        qWarning("kobokeys: Cannot open input device %s: %s", qPrintable(devicePath), strerror(errno));
        return;
    }

    if (m_debug)
    {
        char name[256] = {0};
        ioctl(m_fd, EVIOCGNAME(sizeof(name) - 1), name);
        qDebug() << "kobokeys: Using" << devicePath << name;
    }

    // Keep Nickel (or anything else still running) from also reacting to the buttons
    if (grab)
        this->grab(true);

    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &KoboKeyHandler::readKeyEvents);
}

KoboKeyHandler::~KoboKeyHandler()
{
    if (m_fd >= 0)
    {
        grab(false);
        ::close(m_fd);
    }
}

void KoboKeyHandler::grab(bool enable)
{
    if (m_grabbed == enable)
        return;

    if (ioctl(m_fd, EVIOCGRAB, enable ? 1 : 0) == 0)
        m_grabbed = enable;
    else if (m_debug)
        qDebug("kobokeys: EVIOCGRAB %d on %s failed: %s", enable, qPrintable(m_devicePath), strerror(errno));
}

void KoboKeyHandler::readKeyEvents()
{
    input_event buffer[32];

    for (;;)
    {
        ssize_t n = ::read(m_fd, buffer, sizeof(buffer));
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            if (errno != EAGAIN)
            {
                qWarning("kobokeys: Could not read from %s: %s", qPrintable(m_devicePath), strerror(errno));
                if (errno == ENODEV)
                    m_notifier->setEnabled(false);
            }
            return;
        }
        if (n == 0)
            return;

        // evdev only ever returns whole events
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); i++)
            processKeyEvent(buffer[i], m_debug);
    }
}

void KoboKeyHandler::processKeyEvent(const input_event &ev, bool debug)
{
    if (ev.type != EV_KEY)
        return;

    auto it = KoboPhysicalKeyMap.constFind(ev.code);
    if (it == KoboPhysicalKeyMap.constEnd())
    {
        if (debug)
            qDebug() << "kobokeys: unmapped key code" << ev.code << "value" << ev.value;
        return;
    }

    KoboKey key = it.value();

    // The sleep cover keeps sending repeats for as long as it's closed
    if (key == Key_SleepCover && ev.value == EVENT_REPEAT)
        return;

    QEvent::Type type = ev.value == EVENT_RELEASE ? QEvent::KeyRelease : QEvent::KeyPress;
    bool autoRepeat = ev.value == EVENT_REPEAT;

    if (debug)
        qDebug() << "kobokeys: code" << ev.code << "->" << Qt::hex << int(key) << Qt::dec
                 << (ev.value == EVENT_PRESS ? "pressed" : autoRepeat ? "repeat" : "released");

    // Delivered to the focus window. Safe to call from any thread.
    QWindowSystemInterface::handleExtendedKeyEvent(nullptr, type, key, Qt::NoModifier, ev.code, 0, 0, QString(),
                                                   autoRepeat);
}
