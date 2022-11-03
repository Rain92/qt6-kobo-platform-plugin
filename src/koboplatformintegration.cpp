
#include "koboplatformintegration.h"

#include <QtFbSupport/private/qfbbackingstore_p.h>
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qevdevtouchmanager_p.h>
#include <qpa/qplatforminputcontextfactory_p.h>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtServiceSupport/private/qgenericunixservices_p.h>
#else
#include <QtGui/private/qgenericunixfontdatabase_p.h>
#include <QtGui/private/qgenericunixservices_p.h>
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#endif

KoboPlatformIntegration::KoboPlatformIntegration(const QStringList &paramList)
    : m_paramList(paramList),
      m_primaryScreen(nullptr),
      m_inputContext(nullptr),
      m_fontDb(new QGenericUnixFontDatabase),
      m_services(new QGenericUnixServices),
      m_kbdMgr(nullptr),
      koboKeyboard(nullptr),
      koboAdditions(nullptr),
      debug(false)
{
    koboDevice = determineDevice();

    if (!m_primaryScreen)
        m_primaryScreen = new KoboFbScreen(paramList, &koboDevice);
}

KoboPlatformIntegration::~KoboPlatformIntegration()
{
    QWindowSystemInterface::handleScreenRemoved(m_primaryScreen);
}

void KoboPlatformIntegration::initialize()
{
    if (m_primaryScreen->initialize())
        QWindowSystemInterface::handleScreenAdded(m_primaryScreen);
    else
        qWarning("kobofb: Failed to initialize screen");

    m_inputContext = QPlatformInputContextFactory::create();

    createInputHandlers();

    qWarning("kobofb: Finished initialization.");
}

bool KoboPlatformIntegration::hasCapability(QPlatformIntegration::Capability cap) const
{
    switch (cap)
    {
        case ThreadedPixmaps:
            return false;
        case WindowManagement:
            return false;
        default:
            return QPlatformIntegration::hasCapability(cap);
    }
}

QPlatformBackingStore *KoboPlatformIntegration::createPlatformBackingStore(QWindow *window) const
{
    return new QFbBackingStore(window);
}

QPlatformWindow *KoboPlatformIntegration::createPlatformWindow(QWindow *window) const
{
    return new QFbWindow(window);
}

QAbstractEventDispatcher *KoboPlatformIntegration::createEventDispatcher() const
{
    return QtGenericUnixDispatcher::createUnixEventDispatcher();
}

QList<QPlatformScreen *> KoboPlatformIntegration::screens() const
{
    QList<QPlatformScreen *> list;
    list.append(m_primaryScreen);
    return list;
}

QPlatformFontDatabase *KoboPlatformIntegration::fontDatabase() const
{
    return m_fontDb.data();
}

QPlatformServices *KoboPlatformIntegration::services() const
{
    return m_services.data();
}

void KoboPlatformIntegration::createInputHandlers()
{
    QString touchscreenDevice(koboDevice.touchDev);
    QRegularExpression touchDevRx("touchscreen_device=(.*)");
    QRegularExpression touchSwapXYRx("touchscreen_swap_xy=(.*)");
    QRegularExpression touchInvXRx("touchscreen_invert_x=(.*)");
    QRegularExpression touchInvYRx("touchscreen_invert_y=(.*)");
    QRegularExpression touchRangeXRx("touchscreen_max_range_x=(.*)");
    QRegularExpression touchRangeYRx("touchscreen_max_range_y=(.*)");
    QRegularExpression touchRangeFlipRx("touchscreen_flip_axes_limit=(.*)");

    bool useHWScreenLimits = true;
    bool manualRangeFlip = false;
    int touchRangeX = 0;
    int touchRangeY = 0;
    bool experimentaltouchhandler = false;

    auto screenrot = m_primaryScreen->getScreenRotation();

    for (const QString &arg : qAsConst(m_paramList))
    {
        if (arg.contains("debug"))
            debug = true;

        QRegularExpressionMatch match;
        if (arg.contains(touchDevRx, &match))
            touchscreenDevice = match.captured(1);
        if (arg.contains(touchSwapXYRx, &match) && match.captured(1).toInt() > 0)
        {
            koboDevice.touchscreenSettings.swapXY = true;
        }
        if (arg.contains(touchInvXRx, &match) && match.captured(1).toInt() > 0)
        {
            koboDevice.touchscreenSettings.invertX = true;
        }
        if (arg.contains(touchInvYRx, &match) && match.captured(1).toInt() > 0)
        {
            koboDevice.touchscreenSettings.invertY = true;
        }
        if (arg.contains(touchRangeXRx, &match))
        {
            touchRangeX = match.captured(1).toInt();
        }
        if (arg.contains(touchRangeYRx, &match))
        {
            touchRangeY = match.captured(1).toInt();
        }
        if (arg.contains(touchRangeFlipRx, &match) && match.captured(1).toInt() > 0)
        {
            manualRangeFlip = true;
        }
        if(arg.contains("experimentaltouchhandler"))
        {
            experimentaltouchhandler = true;
        }
    }

    bool flipTouchscreenAxes = koboDevice.touchscreenSettings.swapXY ^ (screenrot & 1);
    if (manualRangeFlip)
        flipTouchscreenAxes = !flipTouchscreenAxes;
    if (useHWScreenLimits && touchRangeX == 0)
        touchRangeX = flipTouchscreenAxes ? koboDevice.height : koboDevice.width;
    if (useHWScreenLimits && touchRangeY == 0)
        touchRangeY = flipTouchscreenAxes ? koboDevice.width : koboDevice.height;

    QString evdevTouchArgs(touchscreenDevice);
    if (koboDevice.touchscreenSettings.swapXY)
        evdevTouchArgs += ":swapxy";
    if (koboDevice.touchscreenSettings.invertX)
        evdevTouchArgs += ":invertx";
    if (koboDevice.touchscreenSettings.invertY)
        evdevTouchArgs += ":inverty";
    if (touchRangeX > 0)
        evdevTouchArgs += QString(":hw_range_x_max=%1").arg(touchRangeX);
    if (touchRangeY > 0)
        evdevTouchArgs += QString(":hw_range_y_max=%1").arg(touchRangeY);
    if(experimentaltouchhandler)
        evdevTouchArgs += ":experimentaltouchhandler";

    evdevTouchArgs += QString(":screenwidth=%1").arg(koboDevice.width);
    evdevTouchArgs += QString(":screenheight=%1").arg(koboDevice.height);

    evdevTouchArgs += QString(":screenrotation=%1").arg(screenrot * 90);

    new QEvdevTouchManager("EvdevTouch", evdevTouchArgs, this, m_primaryScreen);

    koboKeyboard = new KoboButtonIntegration(this, koboDevice.ntxDev, debug);
    koboAdditions = new KoboPlatformAdditions(this, koboDevice);

    if (debug)
        qDebug() << "device:" << koboDevice.modelName << koboDevice.modelNumber << '\n'
                 << "screen:" << koboDevice.width << koboDevice.height << "dpi:" << koboDevice.dpi;
}

QPlatformNativeInterface *KoboPlatformIntegration::nativeInterface() const
{
    return const_cast<KoboPlatformIntegration *>(this);
}

KoboDeviceDescriptor *KoboPlatformIntegration::deviceDescriptor()
{
    return &koboDevice;
}

QFunctionPointer KoboPlatformIntegration::platformFunction(const QByteArray &function) const
{
    if (function == KoboPlatformFunctions::setFrontlightLevelIdentifier())
        return QFunctionPointer(setFrontlightLevelStatic);
    else if (function == KoboPlatformFunctions::getBatteryLevelIdentifier())
        return QFunctionPointer(getBatteryLevelStatic);
    else if (function == KoboPlatformFunctions::isBatteryChargingIdentifier())
        return QFunctionPointer(isBatteryChargingStatic);
    else if (function == KoboPlatformFunctions::setFullScreenRefreshModeIdentifier())
        return QFunctionPointer(setFullScreenRefreshModeStatic);
    else if (function == KoboPlatformFunctions::clearScreenIdentifier())
        return QFunctionPointer(clearScreenStatic);
    else if (function == KoboPlatformFunctions::enableDitheringIdentifier())
        return QFunctionPointer(enableDitheringStatic);
    else if (function == KoboPlatformFunctions::doManualRefreshIdentifier())
        return QFunctionPointer(doManualRefreshStatic);
    else if (function == KoboPlatformFunctions::getKoboDeviceDescriptorIdentifier())
        return QFunctionPointer(getKoboDeviceDescriptorStatic);
    else if (function == KoboPlatformFunctions::testInternetConnectionIdentifier())
        return QFunctionPointer(testInternetConnectionStatic);
    else if (function == KoboPlatformFunctions::enableWiFiConnectionIdentifier())
        return QFunctionPointer(enableWiFiConnectionStatic);
    else if (function == KoboPlatformFunctions::disableWiFiConnectionIdentifier())
        return QFunctionPointer(disableWiFiConnectionStatic);

    return 0;
}

void KoboPlatformIntegration::setFrontlightLevelStatic(int val, int temp)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->koboAdditions->setFrontlightLevel(val, temp);
}

int KoboPlatformIntegration::getBatteryLevelStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return self->koboAdditions->getBatteryLevel();
}

bool KoboPlatformIntegration::isBatteryChargingStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return self->koboAdditions->isBatteryCharging();
}

void KoboPlatformIntegration::setFullScreenRefreshModeStatic(WaveForm waveform)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->setFullScreenRefreshMode(waveform);
}

void KoboPlatformIntegration::clearScreenStatic(bool waitForCompleted)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->clearScreen(waitForCompleted);
}

void KoboPlatformIntegration::enableDitheringStatic(bool softwareDithering, bool hardwareDithering)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->enableDithering(softwareDithering, hardwareDithering);
}

void KoboPlatformIntegration::doManualRefreshStatic(QRect region)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->m_primaryScreen->doManualRefresh(region);
}

KoboDeviceDescriptor KoboPlatformIntegration::getKoboDeviceDescriptorStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return *self->deviceDescriptor();
}

void KoboPlatformIntegration::enableWiFiConnectionStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->wifiManager.enableWiFiConnection();
}

void KoboPlatformIntegration::disableWiFiConnectionStatic()
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    self->wifiManager.disableWiFiConnection();
}

bool KoboPlatformIntegration::testInternetConnectionStatic(int timeout)
{
    KoboPlatformIntegration *self =
        static_cast<KoboPlatformIntegration *>(QGuiApplicationPrivate::platformIntegration());

    return self->wifiManager.testInternetConnection(timeout);
}
