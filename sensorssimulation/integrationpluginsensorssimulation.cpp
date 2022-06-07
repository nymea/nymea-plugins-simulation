/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright 2013 - 2020, nymea GmbH
* Contact: contact@nymea.io
*
* This file is part of nymea.
* This project including source code and documentation is protected by
* copyright law, and remains the property of nymea GmbH. All rights, including
* reproduction, publication, editing and translation, are reserved. The use of
* this project is subject to the terms of a license agreement to be concluded
* with nymea GmbH in accordance with the terms of use of nymea GmbH, available
* under https://nymea.io/license
*
* GNU Lesser General Public License Usage
* Alternatively, this project may be redistributed and/or modified under the
* terms of the GNU Lesser General Public License as published by the Free
* Software Foundation; version 3. This project is distributed in the hope that
* it will be useful, but WITHOUT ANY WARRANTY; without even the implied
* warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public License
* along with this project. If not, see <https://www.gnu.org/licenses/>.
*
* For any further details and any questions please contact us under
* contact@nymea.io or see our FAQ/Licensing Information on
* https://nymea.io/license/faq
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "integrationpluginsensorssimulation.h"
#include "plugininfo.h"

#include <QtMath>
#include <QColor>
#include <QDateTime>
#include <QSettings>

IntegrationPluginSensorsSimulation::IntegrationPluginSensorsSimulation()
{

}

IntegrationPluginSensorsSimulation::~IntegrationPluginSensorsSimulation()
{
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer20Seconds);
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer5Min);
}

void IntegrationPluginSensorsSimulation::init()
{
    // Seed the random generator with current time
    qsrand(QDateTime::currentMSecsSinceEpoch() / 1000);

    // Change some values every 20 seconds
    m_pluginTimer20Seconds = hardwareManager()->pluginTimerManager()->registerTimer(20);
    connect(m_pluginTimer20Seconds, &PluginTimer::timeout, this, &IntegrationPluginSensorsSimulation::onPluginTimer20Seconds);

    // Change some values every 5 min
    m_pluginTimer5Min = hardwareManager()->pluginTimerManager()->registerTimer(300);
    connect(m_pluginTimer5Min, &PluginTimer::timeout, this, &IntegrationPluginSensorsSimulation::onPluginTimer5Minutes);
}

void IntegrationPluginSensorsSimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    qCDebug(dcSensorsSimulation()) << "Setting up thing" << thing->name();
    if (thing->thingClassId() == fingerPrintSensorThingClassId ||
            thing->thingClassId() == barcodeScannerThingClassId ||
            thing->thingClassId() == contactSensorThingClassId ||
            thing->thingClassId() == waterSensorThingClassId) {
        m_simulationTimers.insert(thing, new QTimer(thing));
        connect(m_simulationTimers[thing], &QTimer::timeout, this, &IntegrationPluginSensorsSimulation::simulationTimerTimeout);
    }
    if (thing->thingClassId() == fingerPrintSensorThingClassId && thing->stateValue(fingerPrintSensorUsersStateTypeId).toStringList().count() > 0) {
        m_simulationTimers.value(thing)->start(10000);
    }
    if (thing->thingClassId() == barcodeScannerThingClassId) {
        m_simulationTimers.value(thing)->start(10000);
    }
    if (thing->thingClassId() == contactSensorThingClassId) {
        m_simulationTimers.value(thing)->start(10000);
    }
    if (thing->thingClassId() == waterSensorThingClassId) {
        m_simulationTimers.value(thing)->start(10000);
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginSensorsSimulation::thingRemoved(Thing *thing)
{
    // Clean up any timers we may have for this thing
    if (m_simulationTimers.contains(thing)) {
        QTimer *t = m_simulationTimers.take(thing);
        t->stop();
        t->deleteLater();
    }
}

void IntegrationPluginSensorsSimulation::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();

    if (thing->thingClassId() == fingerPrintSensorThingClassId) {
        if (action.actionTypeId() == fingerPrintSensorAddUserActionTypeId) {
            QStringList users = thing->stateValue(fingerPrintSensorUsersStateTypeId).toStringList();
            QString username = action.param(fingerPrintSensorAddUserActionUserIdParamTypeId).value().toString();
            if (username.isEmpty()) {
                username = "unknown";
            }
            QString finger = action.param(fingerPrintSensorAddUserActionFingerParamTypeId).value().toString();
            QSettings settings;
            settings.beginGroup(thing->id().toString());
            QStringList usedFingers = settings.value(username).toStringList();
            if (users.contains(username) && usedFingers.contains(finger)) {
                return info->finish(Thing::ThingErrorDuplicateUuid);
            }
            QTimer::singleShot(5000, info, [this, info, thing, username, finger]() {
                if (username.toLower().trimmed() == "john") {
                    info->finish(Thing::ThingErrorHardwareFailure, QT_TR_NOOP("Fingerprint could not be scanned. Please try again."));
                } else {
                    info->finish(Thing::ThingErrorNoError);
                    QStringList users = thing->stateValue(fingerPrintSensorUsersStateTypeId).toStringList();
                    if (!users.contains(username)) {
                        users.append(username);
                        thing->setStateValue(fingerPrintSensorUsersStateTypeId, users);
                        m_simulationTimers.value(thing)->start(10000);
                    }

                    QSettings settings;
                    settings.beginGroup(thing->id().toString());
                    QStringList usedFingers = settings.value(username).toStringList();
                    usedFingers.append(finger);
                    settings.setValue(username, usedFingers);
                    settings.endGroup();
                }
            });
            return;
        }
        if (action.actionTypeId() == fingerPrintSensorRemoveUserActionTypeId) {
            QStringList users = thing->stateValue(fingerPrintSensorUsersStateTypeId).toStringList();
            QString username = action.params().first().value().toString();
            if (!users.contains(username)) {
                return info->finish(Thing::ThingErrorInvalidParameter);
            }
            users.removeAll(username);
            thing->setStateValue(fingerPrintSensorUsersStateTypeId, users);
            if (users.count() == 0) {
                m_simulationTimers.value(thing)->stop();
            }
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    qCWarning(dcSensorsSimulation()) << "Unhandled thing class" << thing->thingClassId() << "for" << thing->name();
}

int IntegrationPluginSensorsSimulation::generateRandomIntValue(int min, int max)
{
    int value = ((qrand() % ((max + 1) - min)) + min);
    // qCDebug(dcSimulation()) << "Generateed random int value: [" << min << ", " << max << "] -->" << value;
    return value;
}

double IntegrationPluginSensorsSimulation::generateRandomDoubleValue(double min, double max)
{
    double value = generateRandomIntValue(static_cast<int>(min * 10), static_cast<int>(max * 10)) / 10.0;
    // qCDebug(dcSimulation()) << "Generated random double value: [" << min << ", " << max << "] -->" << value;
    return value;
}

bool IntegrationPluginSensorsSimulation::generateRandomBoolValue()
{
    bool value = static_cast<bool>(generateRandomIntValue(0, 1));
    // qCDebug(dcSimulation()) << "Generated random bool value:" << value;
    return value;
}

qreal IntegrationPluginSensorsSimulation::generateSinValue(int min, int max, int hourOffset, int decimals)
{
    // 00:00 : 23:99 = 0 : PI
    // seconds of day : (60 * 60 * 24) = x : 2*PI
    QDateTime d = QDateTime::currentDateTime();
    int secondsPerDay = 60 * 60 * 24;
    int offsetInSeconds =  hourOffset * 60 * 60;
    int secondsOfDay = d.time().msecsSinceStartOfDay() / 1000;
    // add offset and wrap around
    secondsOfDay = (secondsOfDay - offsetInSeconds) % secondsPerDay;

    qreal interval = secondsOfDay * 2*M_PI / secondsPerDay;
    qreal gain = 1.0 * (max - min) / 2;
    qreal temp = (gain * qSin(interval)) + min + gain;
    return QString::number(temp, 'f', decimals).toDouble();
}

qreal IntegrationPluginSensorsSimulation::generateBatteryValue(int chargeStartHour, int chargeDurationInMinutes)
{
    QDateTime d = QDateTime::currentDateTime();

    int secondsPerDay = 24 * 60 * 60;
    int currentSecond = d.time().msecsSinceStartOfDay() / 1000;
    int chargeStartSecond = chargeStartHour * 60 * 60;
    int chargeEndSecond = chargeStartSecond + (chargeDurationInMinutes * 60);
    int chargeDurationInSeconds = chargeDurationInMinutes * 60;

    // should we be charging?
    if (chargeStartSecond < currentSecond && currentSecond < chargeEndSecond) {
        // Yep, charging...
        int currentChargeSecond = currentSecond - chargeStartSecond;
        // x : 100 = currentChargeSecond : chargeDurationInSeconds
        return 100 * currentChargeSecond / chargeDurationInSeconds;
    }

    int dischargeDurationInSecs = secondsPerDay - chargeDurationInSeconds;
    int currentDischargeSecond;
    if (currentSecond < chargeStartSecond) {
        currentDischargeSecond = currentSecond + (secondsPerDay - chargeEndSecond);
    } else {
        currentDischargeSecond = currentSecond - chargeEndSecond;
    }
    // 100 : x = dischargeDurationInSecs : currentDischargeSecond
    return 100 - (100 * currentDischargeSecond / dischargeDurationInSecs);
}

qreal IntegrationPluginSensorsSimulation::generateNoisyRectangle(int min, int max, int maxNoise, int stablePeriodInMinutes, int &lastValue, QDateTime &lastChangeTimestamp)
{
    QDateTime now = QDateTime::currentDateTime();
    qCDebug(dcSensorsSimulation()) << "Generating noisy rect:" << min << "-" << max << "lastValue:" << lastValue << "lastUpdate" << lastChangeTimestamp << lastChangeTimestamp.secsTo(now) << lastChangeTimestamp.isValid();
    if (!lastChangeTimestamp.isValid() || lastChangeTimestamp.secsTo(now) / 60 > stablePeriodInMinutes) {
        lastChangeTimestamp.swap(now);
        lastValue = min + qrand() % (max - min);
        qCDebug(dcSensorsSimulation()) << "New last value:" << lastValue;
    }
    qreal noise = 0.1 * (qrand() % (maxNoise * 20)  - maxNoise);
    qreal ret = 1.0 * lastValue + noise;
    return ret;
}

void IntegrationPluginSensorsSimulation::onPluginTimer20Seconds()
{
    foreach (Thing *thing, myThings()) {
        if (thing->thingClassId() == temperatureSensorThingClassId) {
            // Temperature sensor
            thing->setStateValue(temperatureSensorTemperatureStateTypeId, generateSinValue(18, 23, 8));
            thing->setStateValue(temperatureSensorHumidityStateTypeId, generateSinValue(40, 55, 20));
            thing->setStateValue(temperatureSensorBatteryLevelStateTypeId, generateBatteryValue(8, 10));
            thing->setStateValue(temperatureSensorBatteryCriticalStateTypeId, thing->stateValue(temperatureSensorBatteryLevelStateTypeId).toInt() <= 25);
            thing->setStateValue(temperatureSensorConnectedStateTypeId, true);
        } else if (thing->thingClassId() == motionDetectorThingClassId) {
            // Motion detector
            thing->setStateValue(motionDetectorIsPresentStateTypeId, generateRandomBoolValue());
            thing->setStateValue(motionDetectorBatteryLevelStateTypeId, generateBatteryValue(13, 1));
            thing->setStateValue(motionDetectorBatteryCriticalStateTypeId, thing->stateValue(motionDetectorBatteryLevelStateTypeId).toInt() <= 30);
            thing->setStateValue(motionDetectorConnectedStateTypeId, true);
        } else if (thing->thingClassId() == waterSensorThingClassId) {
            thing->setStateValue(waterSensorWaterDetectedStateTypeId, generateRandomBoolValue());
        } else if (thing->thingClassId() == gardenSensorThingClassId) {
            // Garden sensor
            thing->setStateValue(gardenSensorTemperatureStateTypeId, generateSinValue(-4, 17, 5));
            thing->setStateValue(gardenSensorSoilMoistureStateTypeId, generateSinValue(40, 60, 13));
            thing->setStateValue(gardenSensorIlluminanceStateTypeId, generateSinValue(0, 80, 2));
            thing->setStateValue(gardenSensorBatteryLevelStateTypeId, generateBatteryValue(9, 20));
            thing->setStateValue(gardenSensorBatteryCriticalStateTypeId, thing->stateValue(gardenSensorBatteryLevelStateTypeId).toDouble() <= 30);
            thing->setStateValue(gardenSensorConnectedStateTypeId, true);
        } else if(thing->thingClassId() == netatmoIndoorThingClassId) {
            // Netatmo
            thing->setStateValue(netatmoIndoorUpdateTimeStateTypeId, QDateTime::currentDateTime().toTime_t());
            thing->setStateValue(netatmoIndoorHumidityStateTypeId, generateSinValue(35, 45, 13));
            thing->setStateValue(netatmoIndoorTemperatureStateTypeId, generateSinValue(20, 25, 3));
            thing->setStateValue(netatmoIndoorPressureStateTypeId, generateSinValue(1003, 1008, 8));
            thing->setStateValue(netatmoIndoorNoiseStateTypeId, generateRandomIntValue(40, 80));
            thing->setStateValue(netatmoIndoorWifiStrengthStateTypeId, generateRandomIntValue(85, 95));
        }
    }
}

void IntegrationPluginSensorsSimulation::onPluginTimer5Minutes()
{
    foreach (Thing *thing, myThings()) {
        if(thing->thingClassId() == netatmoIndoorThingClassId) {
            // Note: should change between > 1000 co2 < 1000 for showcase, please do not change this behaviour
            int currentValue = thing->stateValue(netatmoIndoorCo2StateTypeId).toInt();
            if (currentValue < 1000) {
                thing->setStateValue(netatmoIndoorCo2StateTypeId, generateRandomIntValue(1001, 1010));
            } else {
                thing->setStateValue(netatmoIndoorCo2StateTypeId, generateRandomIntValue(950, 999));
            }
        }
    }
}

void IntegrationPluginSensorsSimulation::simulationTimerTimeout()
{
    QTimer *t = static_cast<QTimer*>(sender());
    Thing *thing = m_simulationTimers.key(t);
    if (thing->thingClassId() == fingerPrintSensorThingClassId) {
        EventTypeId evt = qrand() % 2 == 0 ? fingerPrintSensorAccessGrantedEventTypeId : fingerPrintSensorAccessDeniedEventTypeId;
        ParamList params;
        if (evt == fingerPrintSensorAccessGrantedEventTypeId) {
            QStringList users = thing->stateValue(fingerPrintSensorUsersStateTypeId).toStringList();
            QString user = users.at(qrand() % users.count());
            QSettings settings;
            settings.beginGroup(thing->id().toString());
            QStringList fingers = settings.value(user).toStringList();
            params.append(Param(fingerPrintSensorAccessGrantedEventUserIdParamTypeId, user));
            QString finger = fingers.at(qrand() % fingers.count());
            params.append(Param(fingerPrintSensorAccessGrantedEventFingerParamTypeId, finger));
            qCDebug(dcSensorsSimulation()) << "Emitting fingerprint accepted for user" << user << "and finger" << finger;
        } else {
            qCDebug(dcSensorsSimulation()) << "Emitting fingerprint denied";
        }
        Event event(evt, thing->id(), params);
        emitEvent(event);
    } else if (thing->thingClassId() == barcodeScannerThingClassId) {
        QString code;
        int codeIndex = thing->property("codeIndex").toInt();
        switch (codeIndex) {
        case 0:
            code = "12345";
            thing->setProperty("codeIndex", 1);
            break;
        case 1:
            code = "23456";
            thing->setProperty("codeIndex", 2);
            break;
        default:
            code = "34567";
            thing->setProperty("codeIndex", 0);
            break;
        }

        ParamList params = ParamList() << Param(barcodeScannerCodeScannedEventContentParamTypeId, code);
        Event event(barcodeScannerCodeScannedEventTypeId, thing->id(), params);
        emit emitEvent(event);
    } else if (thing->thingClassId() == contactSensorThingClassId) {
       thing->setStateValue(contactSensorClosedStateTypeId, !thing->stateValue(contactSensorClosedStateTypeId).toBool());
       thing->setStateValue(contactSensorBatteryLevelStateTypeId, thing->stateValue(contactSensorBatteryLevelStateTypeId).toInt()-1);

       if (thing->stateValue(contactSensorBatteryLevelStateTypeId).toInt() == 0) {
           thing->setStateValue(contactSensorBatteryLevelStateTypeId, 100);
           thing->setStateValue(contactSensorBatteryCriticalStateTypeId, false);
       } else if (thing->stateValue(contactSensorBatteryLevelStateTypeId).toInt() <= 20) {
           thing->setStateValue(contactSensorBatteryCriticalStateTypeId, true);
       } else {
           thing->setStateValue(contactSensorBatteryCriticalStateTypeId, false);
       }
    } else if (thing->thingClassId() == waterSensorThingClassId) {
        bool wet = qrand() > (RAND_MAX / 2);
        thing->setStateValue(waterSensorWaterDetectedStateTypeId, wet);
    }
}
