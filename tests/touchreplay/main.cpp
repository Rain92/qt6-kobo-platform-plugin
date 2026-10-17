// Replays recorded/documented Kobo touch event streams through KoboTouchHandler and checks
// the touch events it hands to Qt. Runs on the build machine against a host Qt 6 build:
//
//   cmake -S tests/touchreplay -B build-test -DCMAKE_PREFIX_PATH=~/kobo/qt-bin/qt-6.12.0-host
//   cmake --build build-test && QT_QPA_PLATFORM=offscreen build-test/touchreplay
//
// Event streams are from KOReader's spec/unit/input_spec.lua (Aura H2O) and the traces documented in
// FBInk's fbink_button_scan.c (sunxi, snow/Mk7, Touch A/B).

#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#include <QtGui/QGuiApplication>
#include <QtGui/QPointingDevice>
#include <qpa/qwindowsysteminterface_p.h>

#include <cstdio>
#include <vector>

#include "kobotouchhandler.h"

#ifndef ABS_MT_SLOT
#define ABS_MT_SLOT 0x2f
#endif
#ifndef ABS_MT_DISTANCE
#define ABS_MT_DISTANCE 0x3b
#endif

struct Ev
{
    quint16 type;
    quint16 code;
    qint32 value;
};

static const Ev SYN = {EV_SYN, SYN_REPORT, 0};
static const Ev MT_REPORT = {EV_SYN, SYN_MT_REPORT, 0};
static Ev abs(quint16 code, qint32 value) { return {EV_ABS, code, value}; }
static Ev key(quint16 code, qint32 value) { return {EV_KEY, code, value}; }

class Recorder : public QWindowSystemEventHandler
{
public:
    QStringList log;

    bool sendEvent(QWindowSystemInterfacePrivate::WindowSystemEvent *e) override
    {
        if (e->type == QWindowSystemInterfacePrivate::Touch)
        {
            auto te = static_cast<QWindowSystemInterfacePrivate::TouchEvent *>(e);
            QStringList points;
            for (const QEventPoint &p : te->points)
            {
                const char *state = p.state() == QEventPoint::State::Pressed   ? "P"
                                    : p.state() == QEventPoint::State::Updated ? "M"
                                    : p.state() == QEventPoint::State::Released ? "R"
                                                                                : "S";
                points << QString("%1(%2,%3)")
                              .arg(state)
                              .arg(qRound(p.globalPosition().x()))
                              .arg(qRound(p.globalPosition().y()));
            }
            bool pen = te->device && static_cast<const QPointingDevice *>(te->device)->pointerType() ==
                                         QPointingDevice::PointerType::Pen;
            log << QString(pen ? "pen " : "finger ") + points.join(' ');
            return true;
        }
        if (e->type == QWindowSystemInterfacePrivate::Key)
        {
            auto ke = static_cast<QWindowSystemInterfacePrivate::KeyEvent *>(e);
            log << QString("key %1 %2").arg(ke->keyType == QEvent::KeyPress ? "press" : "release").arg(ke->key, 0, 16);
            return true;
        }
        return QWindowSystemEventHandler::sendEvent(e);
    }
};

static int failures = 0;

static void run(const char *name, const KoboTouchCapabilities &caps, const KoboTouchSettings &settings,
                const std::vector<Ev> &events, const QStringList &expected)
{
    int fds[2];
    if (pipe2(fds, O_NONBLOCK | O_CLOEXEC) != 0)
        qFatal("pipe2 failed");

    Recorder recorder;
    QWindowSystemInterfacePrivate::installWindowSystemEventHandler(&recorder);
    {
        KoboTouchHandler handler(fds[0], caps, settings);
        handler.startReading();

        for (const Ev &e : events)
        {
            input_event ie = {};
            ie.type = e.type;
            ie.code = e.code;
            ie.value = e.value;
            if (write(fds[1], &ie, sizeof(ie)) != sizeof(ie))
                qFatal("write failed");
        }

        for (int i = 0; i < 10; i++)
        {
            QCoreApplication::processEvents();
            QWindowSystemInterface::flushWindowSystemEvents();
        }
        handler.stopReading();
    }
    QWindowSystemInterfacePrivate::removeWindowSystemEventhandler(&recorder);
    close(fds[1]);

    bool ok = recorder.log == expected;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
    {
        failures++;
        printf("  expected:\n");
        for (const QString &s : expected)
            printf("    %s\n", qPrintable(s));
        printf("  got:\n");
        for (const QString &s : recorder.log)
            printf("    %s\n", qPrintable(s));
    }
}

static KoboTouchSettings settingsFor(KoboTouchProtocol protocol, int width, int height, bool swap, bool mirrorX,
                                     bool mirrorY)
{
    KoboTouchSettings s;
    s.protocol = protocol;
    s.uprightWidth = width;
    s.uprightHeight = height;
    s.swapXY = swap;
    s.mirrorX = mirrorX;
    s.mirrorY = mirrorY;
    s.debug = qEnvironmentVariableIsSet("KOBOTOUCH_DEBUG");
    return s;
}

// Multitouch panel whose raw axes are swapped relative to the upright screen
static KoboTouchCapabilities mtCaps(int width, int height, bool withSlots, bool pen = false)
{
    KoboTouchCapabilities c;
    c.name = "test panel";
    c.hasMtPosition = true;
    c.hasMtSlots = withSlots;
    c.maxSlot = withSlots ? 9 : -1;
    c.hasPen = pen;
    c.mtMaxX = height - 1;
    c.mtMaxY = width - 1;
    c.mtPressureMax = 255;
    return c;
}

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    // Elipsa (sunxi): resends the tracking id every frame, lift = TRACKING_ID -1 (fbink_button_scan.c)
    // raw (x, y) -> swap (y, x) -> mirror x: (1403 - y, x)
    auto elipsa = settingsFor(KoboTouchProtocol::Std, 1404, 1872, true, true, false);
    run("std/sunxi single contact", mtCaps(1404, 1872, true), elipsa,
        {abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_TOOL_TYPE, 0), key(BTN_TOUCH, 1), abs(ABS_MT_TOUCH_MAJOR, 5),
         abs(ABS_MT_PRESSURE, 30), abs(ABS_MT_POSITION_X, 500), abs(ABS_MT_POSITION_Y, 300), SYN,
         abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_TOOL_TYPE, 0), abs(ABS_MT_PRESSURE, 31), abs(ABS_MT_POSITION_X, 520),
         abs(ABS_MT_POSITION_Y, 300), SYN,
         abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_TOOL_TYPE, 0), abs(ABS_MT_TOUCH_MAJOR, 0), abs(ABS_MT_PRESSURE, 0),
         abs(ABS_MT_POSITION_X, 520), abs(ABS_MT_POSITION_Y, 300), abs(ABS_MT_TRACKING_ID, -1), key(BTN_TOUCH, 0),
         SYN},
        {"finger P(1103,500)", "finger M(1103,520)", "finger R(1103,520)"});

    // Two fingers; ABS_MT_SLOT is left out again once a single contact is left
    run("std/sunxi two contacts, elided slot", mtCaps(1404, 1872, true), elipsa,
        {abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_PRESSURE, 30), abs(ABS_MT_POSITION_X, 500),
         abs(ABS_MT_POSITION_Y, 300), key(BTN_TOUCH, 1), SYN,
         abs(ABS_MT_SLOT, 0), abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_POSITION_X, 500), abs(ABS_MT_POSITION_Y, 300),
         abs(ABS_MT_SLOT, 1), abs(ABS_MT_TRACKING_ID, 1), abs(ABS_MT_PRESSURE, 30), abs(ABS_MT_POSITION_X, 900),
         abs(ABS_MT_POSITION_Y, 700), SYN,
         abs(ABS_MT_SLOT, 0), abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_POSITION_X, 510), abs(ABS_MT_POSITION_Y, 300),
         abs(ABS_MT_SLOT, 1), abs(ABS_MT_TRACKING_ID, -1), SYN,
         abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_POSITION_X, 520), abs(ABS_MT_POSITION_Y, 300), SYN,
         abs(ABS_MT_TRACKING_ID, 0), abs(ABS_MT_PRESSURE, 0), abs(ABS_MT_TRACKING_ID, -1), key(BTN_TOUCH, 0), SYN},
        {"finger P(1103,500)", "finger S(1103,500) P(703,900)", "finger M(1103,510) R(703,900)",
         "finger M(1103,520)", "finger R(1103,520)"});

    // Stylus: hovering is pressure 0, the barrel buttons are keys
    run("std/sunxi stylus hover, touch, eraser button", mtCaps(1404, 1872, true, true), elipsa,
        {abs(ABS_MT_TRACKING_ID, 1), abs(ABS_MT_TOOL_TYPE, MT_TOOL_PEN), abs(ABS_MT_PRESSURE, 0),
         abs(ABS_MT_DISTANCE, 15), abs(ABS_MT_POSITION_X, 500), abs(ABS_MT_POSITION_Y, 300), SYN,
         abs(ABS_MT_TRACKING_ID, 1), abs(ABS_MT_TOOL_TYPE, MT_TOOL_PEN), abs(ABS_MT_PRESSURE, 40),
         abs(ABS_MT_DISTANCE, 0), SYN,
         key(BTN_STYLUS, 1), SYN,
         abs(ABS_MT_TRACKING_ID, 1), abs(ABS_MT_PRESSURE, 0), abs(ABS_MT_DISTANCE, 15), SYN,
         key(BTN_STYLUS, 0), SYN,
         abs(ABS_MT_TRACKING_ID, -1), SYN},
        {"pen P(1103,500)", "key press 1010001", "pen R(1103,500)", "key release 1010001"});

    // Elipsa 2E / Libra Colour: swap, then mirror *y*, standard kernel MT protocol B
    run("std mirror y (Elipsa 2E)", mtCaps(1404, 1872, true),
        settingsFor(KoboTouchProtocol::Std, 1404, 1872, true, false, true),
        {abs(ABS_MT_SLOT, 0), abs(ABS_MT_TRACKING_ID, 5), abs(ABS_MT_POSITION_X, 500), abs(ABS_MT_POSITION_Y, 300),
         abs(ABS_MT_PRESSURE, 10), key(BTN_TOUCH, 1), SYN,
         abs(ABS_MT_POSITION_X, 520), SYN,
         abs(ABS_MT_TRACKING_ID, -1), key(BTN_TOUCH, 0), SYN},
        {"finger P(300,1371)", "finger M(300,1351)", "finger R(300,1351)"});

    // Clara HD (snow, Mk7): protocol A framing, tracking id never -1, lift = block with pressure 0
    auto claraHD = settingsFor(KoboTouchProtocol::Snow, 1072, 1448, true, true, false);
    auto snowBlock = [](int id, int x, int y, int pressure) {
        return std::vector<Ev>{abs(ABS_MT_TRACKING_ID, id), abs(ABS_MT_DISTANCE, 0), abs(ABS_MT_POSITION_X, x),
                               abs(ABS_MT_POSITION_Y, y), abs(ABS_MT_PRESSURE, pressure),
                               abs(ABS_MT_TOUCH_MAJOR, 0), abs(ABS_MT_TOUCH_MINOR, 0),
                               abs(ABS_MT_ORIENTATION, 0), MT_REPORT};
    };
    auto concat = [](std::initializer_list<std::vector<Ev>> parts) {
        std::vector<Ev> all;
        for (const auto &p : parts)
            all.insert(all.end(), p.begin(), p.end());
        return all;
    };
    run("snow two contacts, lift by pressure and by absence", mtCaps(1072, 1448, false), claraHD,
        concat({{key(BTN_TOOL_FINGER, 1), key(BTN_TOUCH, 1)}, snowBlock(0, 600, 400, 20), {SYN},
                snowBlock(0, 600, 400, 20), snowBlock(1, 1000, 800, 20), {SYN},
                snowBlock(0, 610, 400, 20), {SYN},
                snowBlock(0, 610, 400, 0), {SYN},
                {key(BTN_TOUCH, 0), key(BTN_TOOL_FINGER, 0), SYN}}),
        {"finger P(671,600)", "finger S(671,600) P(271,1000)", "finger M(671,610) R(271,1000)",
         "finger R(671,610)"});

    // Snow panel whose last frame only has BTN_TOUCH 0
    run("snow lift by BTN_TOUCH 0", mtCaps(1072, 1448, false), claraHD,
        concat({{key(BTN_TOOL_FINGER, 1), key(BTN_TOUCH, 1)}, snowBlock(0, 600, 400, 20), {SYN},
                {key(BTN_TOUCH, 0), key(BTN_TOOL_FINGER, 0), SYN}}),
        {"finger P(671,600)", "finger R(671,600)"});

    // A Std device that turns out to use snow framing (KOReader and Plato disagree on the Clara 2E)
    run("std -> snow auto switch", mtCaps(1072, 1448, false),
        settingsFor(KoboTouchProtocol::Std, 1072, 1448, true, true, false),
        concat({{key(BTN_TOUCH, 1)}, snowBlock(0, 600, 400, 20), {SYN}, snowBlock(0, 600, 400, 0), {SYN},
                {key(BTN_TOUCH, 0), SYN}}),
        {"finger P(671,600)", "finger R(671,600)"});

    // A newer, sane revision of a snow panel: sends TRACKING_ID -1 and no protocol A framing
    run("snow -> std auto switch", mtCaps(1072, 1448, true), claraHD,
        {abs(ABS_MT_TRACKING_ID, 3), abs(ABS_MT_POSITION_X, 600), abs(ABS_MT_POSITION_Y, 400),
         abs(ABS_MT_PRESSURE, 20), key(BTN_TOUCH, 1), SYN,
         abs(ABS_MT_POSITION_X, 610), SYN,
         abs(ABS_MT_TRACKING_ID, -1), key(BTN_TOUCH, 0), SYN},
        {"finger P(671,600)", "finger M(671,610)", "finger R(671,610)"});

    // Aura H2O (phoenix): the recorded trace from KOReader's input_spec.lua, lift = TOUCH_MAJOR 0
    auto h2o = settingsFor(KoboTouchProtocol::Phoenix, 1080, 1430, true, true, false);
    auto phoenixBlock = [](int major, int x, int y) {
        return std::vector<Ev>{abs(ABS_MT_TRACKING_ID, 1), abs(ABS_MT_TOUCH_MAJOR, major),
                               abs(ABS_MT_WIDTH_MAJOR, major), abs(ABS_MT_POSITION_X, x),
                               abs(ABS_MT_POSITION_Y, y), MT_REPORT, SYN};
    };
    run("phoenix recorded Aura H2O trace", mtCaps(1080, 1430, false), h2o,
        concat({phoenixBlock(1, 1012, 914), phoenixBlock(1, 1012, 915), phoenixBlock(1, 1010, 918),
                phoenixBlock(0, 1010, 918)}),
        {"finger P(165,1012)", "finger M(164,1012)", "finger M(161,1010)", "finger R(161,1010)"});

    // Touch B (legacy single touch): the lift frame carries coordinates the kernel already transformed
    KoboTouchCapabilities touchB;
    touchB.stMaxX = 799;
    touchB.stMaxY = 599;
    touchB.stPressureMax = 100;
    run("legacy Touch B", touchB, settingsFor(KoboTouchProtocol::Legacy, 600, 800, true, true, false),
        {abs(ABS_Y, 100), abs(ABS_X, 200), abs(ABS_PRESSURE, 100), key(BTN_TOUCH, 1), SYN,
         abs(ABS_Y, 499), abs(ABS_X, 200), abs(ABS_PRESSURE, 0), key(BTN_TOUCH, 0), SYN,
         abs(ABS_Y, 50), abs(ABS_X, 300), abs(ABS_PRESSURE, 100), key(BTN_TOUCH, 1), SYN,
         abs(ABS_PRESSURE, 0), key(BTN_TOUCH, 0), SYN},
        {"finger P(499,200)", "finger R(499,200)", "finger P(549,300)", "finger R(549,300)"});

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
