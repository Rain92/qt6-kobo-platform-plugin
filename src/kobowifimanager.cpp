#include "kobowifimanager.h"

#include <QDebug>
#include <QFile>
#include <QString>
#include <QThread>

KoboWifiManager::KoboWifiManager() : process(nullptr) {}

KoboWifiManager::~KoboWifiManager()
{
    if (process)
    {
        process->disconnect();
        stopProcess();
    }
}

void KoboWifiManager::executeShell(const char* command)
{
    if (!process)
    {
        process.reset(new QProcess());
        QObject::connect(process.data(), &QProcess::readyReadStandardOutput,
                         [&]() { qDebug() << process->readAllStandardOutput(); });

        QObject::connect(process.data(), &QProcess::readyReadStandardError,
                         [&]() { qDebug() << process->readAllStandardError(); });
    }

    stopProcess();
    process->start("/bin/sh", {}, QProcess::ReadWrite);
    process->waitForStarted();

    process->write(command);
    process->write("\nexit\n");
    process->waitForFinished();
}

void KoboWifiManager::stopProcess()
{
    if (process)
        if (process->state() != QProcess::NotRunning)
            process->close();
}

bool KoboWifiManager::testInternetConnection(int timeout)
{
    QString cmd = QString("ping -c 1 -q -W %1 1.1.1.1 2>&1 >/dev/null").arg(timeout);
    int res = system(cmd.toLocal8Bit());
    return res == 0;
}

void KoboWifiManager::enableWiFiConnection()
{
    QFile restoreWifiFile(":/scripts/restore-wifi.sh");
    if (!restoreWifiFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "Failed to load" << restoreWifiFile.fileName();
        return;
    }
    QByteArray restoreWifiScript = restoreWifiFile.readAll();
    restoreWifiFile.close();

    executeShell(restoreWifiScript.data());
}

void KoboWifiManager::disableWiFiConnection()
{
    QFile disableWifiFile(":/scripts/disable-wifi.sh");
    if (!disableWifiFile.open(QIODevice::ReadOnly))
    {
        qWarning() << "Failed to load" << disableWifiFile.fileName();
        return;
    }
    QByteArray disableWifiScript = disableWifiFile.readAll();
    disableWifiFile.close();

    executeShell(disableWifiScript.data());
}
