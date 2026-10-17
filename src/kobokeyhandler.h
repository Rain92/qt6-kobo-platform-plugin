#ifndef KOBOKEYHANDLER_H
#define KOBOKEYHANDLER_H

#include <linux/input.h>

#include <QObject>
#include <QString>

class QSocketNotifier;

// Reads the hardware buttons (power, sleep cover, page turn, home, light) from one evdev device
// and forwards them as Qt key events. Runs in the gui thread.
class KoboKeyHandler : public QObject
{
    Q_OBJECT

public:
    // fd: an already opened (non-blocking) fd for devicePath, or -1 to open it here
    KoboKeyHandler(const QString &devicePath, int fd, bool grab, bool debug, QObject *parent = nullptr);
    ~KoboKeyHandler();

    bool isValid() const { return m_fd >= 0; }
    QString devicePath() const { return m_devicePath; }

    // Shared with the touch handler, which receives the stylus buttons. Thread safe.
    static void processKeyEvent(const input_event &ev, bool debug);

private:
    void readKeyEvents();
    void grab(bool enable);

    QString m_devicePath;
    int m_fd;
    QSocketNotifier *m_notifier;
    bool m_grabbed;
    bool m_debug;
};

#endif  // KOBOKEYHANDLER_H
