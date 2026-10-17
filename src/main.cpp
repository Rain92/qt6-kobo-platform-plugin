
#include <qpa/qplatformintegrationplugin.h>

#include "koboplatformintegration.h"

class KoboPlatformIntegrationPlugin : public QPlatformIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformIntegrationFactoryInterface_iid FILE "../koboplatformplugin.json")

public:
    QPlatformIntegration* create(const QString&, const QStringList&) override;
};

QPlatformIntegration* KoboPlatformIntegrationPlugin::create(const QString& system,
                                                            const QStringList& paramList)
{
    if (!system.compare("kobo", Qt::CaseInsensitive))
        return new KoboPlatformIntegration(paramList);

    return nullptr;
}

#include "main.moc"
