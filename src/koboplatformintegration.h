#ifndef QLINUXFBINTEGRATION_H
#define QLINUXFBINTEGRATION_H

#include <qpa/qplatformintegration.h>
#include <qpa/qplatformnativeinterface.h>

#include <QtCore/QRegularExpression>

#include "kobobuttonintegration.h"
#include "kobofbscreen.h"
#include "koboplatformadditions.h"
#include "koboplatformfunctions.h"
#include "kobowifimanager.h"

class QAbstractEventDispatcher;
class QFbVtHandler;
class QEvdevKeyboardManager;

class KoboPlatformIntegration : public QPlatformIntegration, public QPlatformNativeInterface
{
public:
    explicit KoboPlatformIntegration(const QStringList &paramList);
    ~KoboPlatformIntegration();

    void initialize() override;
    bool hasCapability(QPlatformIntegration::Capability cap) const override;

    QPlatformWindow *createPlatformWindow(QWindow *window) const override;
    QPlatformBackingStore *createPlatformBackingStore(QWindow *window) const override;

    QAbstractEventDispatcher *createEventDispatcher() const override;

    QPlatformFontDatabase *fontDatabase() const override;
    QPlatformServices *services() const override;
    QPlatformInputContext *inputContext() const override { return m_inputContext; }

    QPlatformNativeInterface *nativeInterface() const override;
    QFunctionPointer platformFunction(const QByteArray &function) const override;

    QList<QPlatformScreen *> screens() const;


    KoboDeviceDescriptor *deviceDescriptor();

private:
    void createInputHandlers();
    static int getBatteryLevelStatic();
    static bool isBatteryChargingStatic();
    static void setFrontlightLevelStatic(int val, int temp);
    static void setFullScreenRefreshModeStatic(WaveForm waveform);
    static void clearScreenStatic(bool waitForCompleted);
    static void enableDitheringStatic(bool softwareDithering, bool hardwareDithering);
    static void doManualRefreshStatic(QRect region);
    static KoboDeviceDescriptor getKoboDeviceDescriptorStatic();
    static void enableWiFiConnectionStatic();
    static void disableWiFiConnectionStatic();
    static bool testInternetConnectionStatic(int timeout);

    KoboDeviceDescriptor koboDevice;

    KoboWifiManager wifiManager;

    QStringList m_paramList;
    KoboFbScreen *m_primaryScreen;
    QPlatformInputContext *m_inputContext;
    QScopedPointer<QPlatformFontDatabase> m_fontDb;
    QScopedPointer<QPlatformServices> m_services;

    QEvdevKeyboardManager *m_kbdMgr;
    KoboButtonIntegration *koboKeyboard;
    KoboPlatformAdditions *koboAdditions;
    bool debug;
};

#endif  // QLINUXFBINTEGRATION_H
