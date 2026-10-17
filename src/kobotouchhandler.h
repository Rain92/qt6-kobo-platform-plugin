#ifndef KOBOTOUCHHANDLER_H
#define KOBOTOUCHHANDLER_H

#include <linux/input.h>
#include <qpa/qwindowsysteminterface.h>

#include <QtCore/private/qthread_p.h>

#include <QObject>
#include <QString>
#include <QVector>

#include "koboinputmanager.h"

class QSocketNotifier;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
class QPointingDevice;
#else
class QTouchDevice;
#endif

// What the panel reports through EVIOCGBIT/EVIOCGABS. A max <= min means the axis doesn't exist.
struct KoboTouchCapabilities
{
    QString name;
    bool hasMtSlots = false;
    bool hasMtPosition = false;
    bool hasPen = false;
    int maxSlot = -1;
    int mtMinX = 0, mtMaxX = 0, mtMinY = 0, mtMaxY = 0;
    int stMinX = 0, stMaxX = 0, stMinY = 0, stMaxY = 0;
    int mtPressureMin = 0, mtPressureMax = 0;
    int stPressureMin = 0, stPressureMax = 0;
};

// Decodes the evdev events of one Kobo touch panel and sends them to Qt as touch events.
//
// Kobo touch panels use one of four different conventions for reporting contacts and lifts
// (see KoboTouchProtocol). The protocol is picked per device model, with the same runtime
// fallbacks KOReader uses for panels that turn out to behave differently.
// Coordinates are then fixed up with the swap/mirror quirks from FBInk's device database and the
// current screen rotation.
class KoboTouchHandler : public QObject
{
    Q_OBJECT

public:
    // fd: an already opened (non-blocking) fd for devicePath, or -1 to open it here
    KoboTouchHandler(const QString &devicePath, int fd, const KoboTouchSettings &settings,
                     QObject *parent = nullptr);
    // For replaying recorded events from a pipe (tests/touchreplay)
    KoboTouchHandler(int fd, const KoboTouchCapabilities &caps, const KoboTouchSettings &settings,
                     QObject *parent = nullptr);
    ~KoboTouchHandler();

    static KoboTouchCapabilities queryCapabilities(int fd);

    bool isValid() const { return m_fd >= 0; }
    QString devicePath() const { return m_devicePath; }

    // Called from the handler thread
    void startReading();
    void stopReading();

private:
    enum Tool
    {
        ToolFinger,
        ToolPen,
        ToolCount
    };

    struct Contact
    {
        int trackingId = -1;
        int x = 0;
        int y = 0;
        int pressure = 0;
        Tool tool = ToolFinger;
        // State the decoder has seen so far
        bool down = false;
        bool hasPosition = false;
        bool moved = false;
        // Reported in the current protocol A frame
        bool seenInFrame = false;
        // New tracking id while still down, i.e. the driver skipped the lift
        bool restart = false;
        // State that was last sent to Qt
        bool reportedDown = false;
        int qtId = -1;
        QPointF lastPos;
    };

    void probeDevice();
    void applyCapabilities(const KoboTouchCapabilities &caps);
    void registerDevices();
    void unregisterDevices();

    void readData();
    void processEvent(const input_event &ev);
    void processAbs(const input_event &ev);
    void processKey(const input_event &ev);
    void commitFrame();
    void releaseAll();
    void selectSlotForTrackingId(int trackingId);
    void switchProtocol(KoboTouchProtocol protocol, const char *reason);

    bool isEffectivelyDown(const Contact &c) const;
    QPointF transform(int rawX, int rawY) const;
    QRectF screenGeometry() const;

    QString m_devicePath;
    KoboTouchSettings m_settings;
    KoboTouchProtocol m_protocol;
    int m_fd;
    QSocketNotifier *m_notifier;
    QString m_name;

    // Capabilities
    bool m_hasMtSlots;
    bool m_hasMtPosition;
    bool m_hasMtPressure;
    bool m_hasStPressure;
    bool m_hasPen;
    int m_maxSlots;
    int m_rawMinX, m_rawMaxX, m_rawMinY, m_rawMaxY;
    int m_pressureMin, m_pressureMax;

    // Decoder state
    QVector<Contact> m_contacts;  // indexed by slot
    int m_currentSlot;
    bool m_frameHasMtData;
    bool m_sawLiftTrackingId;
    bool m_dropping;
    int m_nextQtId;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointingDevice *m_devices[ToolCount];
#else
    QTouchDevice *m_device;
#endif
};

class KoboTouchHandlerThread : public QDaemonThread
{
    Q_OBJECT

public:
    KoboTouchHandlerThread(const QString &devicePath, int fd, const KoboTouchSettings &settings,
                           QObject *parent = nullptr);
    ~KoboTouchHandlerThread();

    bool isValid() const { return m_handler && m_handler->isValid(); }

protected:
    void run() override;

private:
    KoboTouchHandler *m_handler;
};

#endif  // KOBOTOUCHHANDLER_H
