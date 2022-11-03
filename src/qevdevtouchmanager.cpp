/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the plugins module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <private/qguiapplication_p.h>
#include <private/qinputdevicemanager_p_p.h>

#include <QGuiApplication>
#include <QLoggingCategory>
#include <QStringList>

#include "qevdevtouchhandlerthread.h"
#include "qevdevtouchmanager_p.h"

QT_BEGIN_NAMESPACE

Q_DECLARE_LOGGING_CATEGORY(qLcEvdevTouch)
Q_DECLARE_LOGGING_CATEGORY(qLcEvdevTouch2)
Q_DECLARE_LOGGING_CATEGORY(qLcEvdevTouch3)

QEvdevTouchManager::QEvdevTouchManager(const QString &key, const QString &specification, QObject *parent, KoboFbScreen *koboFbScreen)
    : QObject(parent), koboFbScreen(koboFbScreen)
{
    Q_UNUSED(key);

    if (qEnvironmentVariableIsSet("QT_QPA_EVDEV_DEBUG"))
    {
        const_cast<QLoggingCategory &>(qLcEvdevTouch()).setEnabled(QtDebugMsg, true);
        const_cast<QLoggingCategory &>(qLcEvdevTouch2()).setEnabled(QtDebugMsg, true);
        const_cast<QLoggingCategory &>(qLcEvdevTouch3()).setEnabled(QtDebugMsg, true);
    }

    QString spec = QString::fromLocal8Bit(qgetenv("QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"));

    if (spec.isEmpty())
        spec = specification;

    m_spec = spec;

    auto args = spec.split(QLatin1Char(':'));

    for (const auto &arg : qAsConst(args))
    {
        if (arg.startsWith(QLatin1String("/dev/")))
        {
            // if device is specified try to use it
            devicePaths.append(arg);
        }
        else
        {
            if (!spec.isEmpty())
                spec += QLatin1Char(':');
            // build new specification without /dev/ elements
            spec += arg;
        }
    }


    for (const QString &device : qAsConst(devicePaths))
        addDevice(device);
}

QEvdevTouchManager::~QEvdevTouchManager() {}

void QEvdevTouchManager::addDevice(const QString &deviceNode)
{
    qCDebug(qLcEvdevTouch, "evdevtouch: Adding device at %ls", qUtf16Printable(deviceNode));
    auto handler = std::unique_ptr<QEvdevTouchScreenHandlerThread>{
        new QEvdevTouchScreenHandlerThread(deviceNode, m_spec, this, koboFbScreen)};
    if (handler)
    {
        connect(handler.get(), &QEvdevTouchScreenHandlerThread::touchDeviceRegistered, this,
                &QEvdevTouchManager::updateInputDeviceCount);
        m_activeDevices.push_back({deviceNode, std::move(handler)});
    }
    else
    {
        qWarning("evdevtouch: Failed to open touch device %ls", qUtf16Printable(deviceNode));
    }
}

void QEvdevTouchManager::removeDevice(const QString &deviceNode)
{
    for (uint i = 0; i < m_activeDevices.size(); i++)
    {
        if (m_activeDevices[i].deviceNode == deviceNode)
        {
            m_activeDevices.erase(m_activeDevices.begin() + i);

            qCDebug(qLcEvdevTouch, "evdevtouch: Removing device at %ls", qUtf16Printable(deviceNode));
            updateInputDeviceCount();
        }
    }
}

void QEvdevTouchManager::updateInputDeviceCount()
{
    int registeredTouchDevices = 0;
    for (const auto &device : m_activeDevices)
    {
        if (device.handler->isTouchDeviceRegistered())
            ++registeredTouchDevices;
    }

    qCDebug(qLcEvdevTouch,
            "evdevtouch: Updating QInputDeviceManager device count: %d touch devices, %d pending handler(s)",
            registeredTouchDevices, m_activeDevices.size() - registeredTouchDevices);

        QInputDeviceManagerPrivate::get(QGuiApplicationPrivate::inputDeviceManager())
            ->setDeviceCount(QInputDeviceManager::DeviceTypeTouch, registeredTouchDevices);
}

QT_END_NAMESPACE
