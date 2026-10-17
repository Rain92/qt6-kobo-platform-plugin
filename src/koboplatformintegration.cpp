
#include "koboplatformintegration.h"

#include <QtFbSupport/private/qfbbackingstore_p.h>
#include <QtFbSupport/private/qfbwindow_p.h>
#include <QtGui/private/qguiapplication_p.h>
#include <qpa/qplatforminputcontextfactory_p.h>
#include <qpa/qplatformservices.h>

#include "koboinputmanager.h"

// Qt 6 closes a widget's window through QWindow::close(), which destroys the native window but keeps the widget's
// backing store, and the next show() creates a new native window for it. QFbScreen only attaches the backing
// stores that were created before their native window (QFbScreen::addWindow()), so a dialog that is opened
// again after it was closed stays blank, while it is in the window stack and gets the input (an invisible
// popup). The backing stores are remembered and attached to the new native window.
namespace
{
QHash<const QWindow *, QFbBackingStore *> liveBackingStores;

class KoboBackingStore : public QFbBackingStore
{
public:
    explicit KoboBackingStore(QWindow *window) : QFbBackingStore(window)
    {
        liveBackingStores.insert(window, this);
    }

    ~KoboBackingStore() override
    {
        if (liveBackingStores.value(window()) == this)
            liveBackingStores.remove(window());
    }
};
}  // namespace

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QtEventDispatcherSupport/private/qgenericunixeventdispatcher_p.h>
#include <QtFontDatabaseSupport/private/qgenericunixfontdatabase_p.h>
#include <QtServiceSupport/private/qgenericunixservices_p.h>
#else
#include <QtGui/private/qgenericunixeventdispatcher_p.h>
#include <QtGui/private/qgenericunixfontdatabase_p.h>
#endif

KoboPlatformIntegration::KoboPlatformIntegration(const QStringList &paramList)
    : m_paramList(paramList),
      m_primaryScreen(nullptr),
      m_inputContext(nullptr),
      m_fontDb(new QGenericUnixFontDatabase),
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
      m_services(new QGenericUnixServices),
#else
      // Like linuxfb: the desktop services would only pull in xdg/dbus code that's useless on a Kobo
      m_services(new QPlatformServices),
#endif
      m_inputManager(nullptr),
      koboAdditions(nullptr),
      debug(false)
{
    for (const QString &arg : paramList)
        if (arg == QLatin1String("debug"))
            debug = true;

    koboDevice = determineDevice(debug);

    m_primaryScreen = new KoboFbScreen(paramList, &koboDevice);
}

KoboPlatformIntegration::~KoboPlatformIntegration()
{
    // The touch handler threads use the screen, so stop them before the screen goes away
    delete m_inputManager;
    m_inputManager = nullptr;

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

    if (debug)
        qDebug("kobofb: Finished initialization.");
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
    return new KoboBackingStore(window);
}

QPlatformWindow *KoboPlatformIntegration::createPlatformWindow(QWindow *window) const
{
    auto fbWindow = new QFbWindow(window);

    // the window is shown again after it was closed, its backing store is still there
    if (auto backingStore = liveBackingStores.value(window))
        fbWindow->setBackingStore(backingStore);

    return fbWindow;
}

QAbstractEventDispatcher *KoboPlatformIntegration::createEventDispatcher() const
{
    return createUnixEventDispatcher();
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
    m_inputManager = new KoboInputManager(m_primaryScreen, &koboDevice, m_paramList, debug);

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

    return nullptr;
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
