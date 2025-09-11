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

#include "integrationpluginappliancessimulation.h"
#include "plugininfo.h"

#include <QtMath>
#include <QColor>
#include <QDateTime>
#include <QSettings>

IntegrationPluginAppliancesSimulation::IntegrationPluginAppliancesSimulation()
{

}

IntegrationPluginAppliancesSimulation::~IntegrationPluginAppliancesSimulation()
{
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer20Seconds);
}

void IntegrationPluginAppliancesSimulation::init()
{
    // Seed the random generator with current time
    std::srand(QDateTime::currentMSecsSinceEpoch() / 1000);

    // Change some values every 20 seconds
    m_pluginTimer20Seconds = hardwareManager()->pluginTimerManager()->registerTimer(20);
    connect(m_pluginTimer20Seconds, &PluginTimer::timeout, this, &IntegrationPluginAppliancesSimulation::onPluginTimer20Seconds);
}

void IntegrationPluginAppliancesSimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    qCDebug(dcAppliancesSimulation()) << "Setting up thing" << thing->name();
    if (thing->thingClassId() == cleaningRobotThingClassId) {
        m_simulationTimers.insert(thing, new QTimer(thing));
        connect(m_simulationTimers[thing], &QTimer::timeout, this, &IntegrationPluginAppliancesSimulation::simulationTimerTimeout);
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginAppliancesSimulation::thingRemoved(Thing *thing)
{
    // Clean up any timers we may have for this thing
    if (m_simulationTimers.contains(thing)) {
        QTimer *t = m_simulationTimers.take(thing);
        t->stop();
        t->deleteLater();
    }
}

void IntegrationPluginAppliancesSimulation::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();
    if (thing->thingClassId() == cleaningRobotThingClassId) {
        if (action.actionTypeId() == cleaningRobotStartCleaningActionTypeId) {
            qCDebug(dcAppliancesSimulation()) << "Starting to clean...";
            thing->setStateValue(cleaningRobotRobotStateStateTypeId, "cleaning");
            m_simulationTimers.value(thing)->stop();
            info->finish(Thing::ThingErrorNoError);
            return;
        }
        if (action.actionTypeId() == cleaningRobotPauseCleaningActionTypeId) {
            qCDebug(dcAppliancesSimulation()) << "Pausing...";
            if (thing->stateValue(cleaningRobotRobotStateStateTypeId).toString() == "paused") {
                thing->setStateValue(cleaningRobotRobotStateStateTypeId, "cleaning");
            } else if (thing->stateValue(cleaningRobotRobotStateStateTypeId).toString() == "cleaning"){
                thing->setStateValue(cleaningRobotRobotStateStateTypeId, "paused");
            }
            info->finish(Thing::ThingErrorNoError);
            return;
        }
        if (action.actionTypeId() == cleaningRobotStopCleaningActionTypeId) {
            qCDebug(dcAppliancesSimulation()) << "Stopping.";
            thing->setStateValue(cleaningRobotRobotStateStateTypeId, "stopped");
            info->finish(Thing::ThingErrorNoError);
            return;
        }
        if (action.actionTypeId() == cleaningRobotReturnToBaseActionTypeId) {
            qCDebug(dcAppliancesSimulation()) << "Returning to base...";
            QString robotState = thing->stateValue(cleaningRobotRobotStateStateTypeId).toString();
            if (robotState == "cleaning" || robotState == "paused" || robotState == "error") {
                thing->setStateValue(cleaningRobotRobotStateStateTypeId, "traveling");
                m_simulationTimers.value(thing)->start(5000);
            }
            info->finish(Thing::ThingErrorNoError);
            return;
        }
        if (action.actionTypeId() == cleaningRobotSimulateErrorActionTypeId) {
            thing->setStateValue(cleaningRobotRobotStateStateTypeId, "error");
            thing->setStateValue(cleaningRobotErrorMessageStateTypeId, QT_TR_NOOP("Help me, I'm stuck!"));
            info->finish(Thing::ThingErrorNoError);
            return;
        }
    }

    qCWarning(dcAppliancesSimulation()) << "Unhandled thing class" << thing->thingClassId() << "for" << thing->name();
}

int IntegrationPluginAppliancesSimulation::generateRandomIntValue(int min, int max)
{
    int value = ((std::rand() % ((max + 1) - min)) + min);
    // qCDebug(dcSimulation()) << "Generateed random int value: [" << min << ", " << max << "] -->" << value;
    return value;
}

double IntegrationPluginAppliancesSimulation::generateRandomDoubleValue(double min, double max)
{
    double value = generateRandomIntValue(static_cast<int>(min * 10), static_cast<int>(max * 10)) / 10.0;
    // qCDebug(dcSimulation()) << "Generated random double value: [" << min << ", " << max << "] -->" << value;
    return value;
}

bool IntegrationPluginAppliancesSimulation::generateRandomBoolValue()
{
    bool value = static_cast<bool>(generateRandomIntValue(0, 1));
    // qCDebug(dcSimulation()) << "Generated random bool value:" << value;
    return value;
}

qreal IntegrationPluginAppliancesSimulation::generateSinValue(int min, int max, int hourOffset, int decimals)
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

qreal IntegrationPluginAppliancesSimulation::generateBatteryValue(int chargeStartHour, int chargeDurationInMinutes)
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


void IntegrationPluginAppliancesSimulation::onPluginTimer20Seconds()
{
    foreach (Thing *thing, myThings()) {
        if (thing->thingClassId() == cleaningRobotThingClassId) {
            QString robotState = thing->stateValue(cleaningRobotRobotStateStateTypeId).toString();
            int batteryLevel = thing->stateValue(cleaningRobotBatteryLevelStateTypeId).toInt();
            bool charging = false;
            bool pluggedIn = false;
            if (robotState == "cleaning") {
                batteryLevel -= 1;
                if (batteryLevel < 5) {
                    robotState = "traveling";
                    m_simulationTimers.value(thing)->start(5000);
                }
            } else if (robotState == "docked") {
                batteryLevel = qMin(100, batteryLevel + 2);
                charging = batteryLevel < 100;
                pluggedIn = true;
            }
            thing->setStateValue(cleaningRobotRobotStateStateTypeId, robotState);
            thing->setStateValue(cleaningRobotBatteryLevelStateTypeId, batteryLevel);
            thing->setStateValue(cleaningRobotBatteryCriticalStateTypeId, batteryLevel < 10);
            thing->setStateValue(cleaningRobotChargingStateTypeId, charging);
            thing->setStateValue(cleaningRobotPluggedInStateTypeId, pluggedIn);
        }
    }
}

void IntegrationPluginAppliancesSimulation::simulationTimerTimeout()
{
    QTimer *t = static_cast<QTimer*>(sender());
    Thing *thing = m_simulationTimers.key(t);
    if (thing->thingClassId() == cleaningRobotThingClassId) {
        thing->setStateValue(cleaningRobotRobotStateStateTypeId, "docked");
    }
}
