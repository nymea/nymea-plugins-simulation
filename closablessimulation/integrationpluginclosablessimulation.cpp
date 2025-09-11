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

#include "integrationpluginclosablessimulation.h"
#include "plugininfo.h"

#include <QtMath>
#include <QColor>
#include <QDateTime>
#include <QSettings>

IntegrationPluginSimulation::IntegrationPluginSimulation()
{

}

IntegrationPluginSimulation::~IntegrationPluginSimulation()
{
}

void IntegrationPluginSimulation::init()
{
    // Seed the random generator with current time
    std::srand(QDateTime::currentMSecsSinceEpoch() / 1000);
}

void IntegrationPluginSimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();
    qCDebug(dcClosablesSimulation()) << "Setting up thing" << thing->name();
    if (thing->thingClassId() == garageGateThingClassId ||
            thing->thingClassId() == extendedAwningThingClassId ||
            thing->thingClassId() == extendedBlindThingClassId ||
            thing->thingClassId() == venetianBlindThingClassId ||
            thing->thingClassId() == rollerShutterThingClassId) {
        m_simulationTimers.insert(thing, new QTimer(thing));
        connect(m_simulationTimers[thing], &QTimer::timeout, this, &IntegrationPluginSimulation::simulationTimerTimeout);
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginSimulation::thingRemoved(Thing *thing)
{
    // Clean up any timers we may have for this thing
    if (m_simulationTimers.contains(thing)) {
        QTimer *t = m_simulationTimers.take(thing);
        t->stop();
        t->deleteLater();
    }
}

void IntegrationPluginSimulation::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();

    if (thing->thingClassId() == garageGateThingClassId) {
        if (action.actionTypeId() == garageGateOpenActionTypeId) {
            if (thing->stateValue(garageGateStateStateTypeId).toString() == "opening") {
                qCDebug(dcClosablesSimulation()) << "Garage gate already opening.";
                return info->finish(Thing::ThingErrorNoError);
            }
            if (thing->stateValue(garageGateStateStateTypeId).toString() == "open" &&
                    !thing->stateValue(garageGateIntermediatePositionStateTypeId).toBool()) {
                qCDebug(dcClosablesSimulation()) << "Garage gate already open.";
                return info->finish(Thing::ThingErrorNoError);
            }
            thing->setStateValue(garageGateStateStateTypeId, "opening");
            thing->setStateValue(garageGateIntermediatePositionStateTypeId, true);
            m_simulationTimers.value(thing)->start(5000);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == garageGateCloseActionTypeId) {
            if (thing->stateValue(garageGateStateStateTypeId).toString() == "closing") {
                qCDebug(dcClosablesSimulation()) << "Garage gate already closing.";
                return info->finish(Thing::ThingErrorNoError);
            }
            if (thing->stateValue(garageGateStateStateTypeId).toString() == "closed" &&
                    !thing->stateValue(garageGateIntermediatePositionStateTypeId).toBool()) {
                qCDebug(dcClosablesSimulation()) << "Garage gate already closed.";
                return info->finish(Thing::ThingErrorNoError);
            }
            thing->setStateValue(garageGateStateStateTypeId, "closing");
            thing->setStateValue(garageGateIntermediatePositionStateTypeId, true);
            m_simulationTimers.value(thing)->start(5000);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == garageGateStopActionTypeId) {
            if (thing->stateValue(garageGateStateStateTypeId).toString() == "opening" ||
                    thing->stateValue(garageGateStateStateTypeId).toString() == "closing") {
                thing->setStateValue(garageGateStateStateTypeId, "open");
                return info->finish(Thing::ThingErrorNoError);
            }
            qCDebug(dcClosablesSimulation()) << "Garage gate not moving";
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == garageGatePowerActionTypeId) {
            bool power = action.param(garageGatePowerActionPowerParamTypeId).value().toBool();
            thing->setStateValue(garageGatePowerStateTypeId, power);
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    if (thing->thingClassId() == rollerShutterThingClassId) {
        if (action.actionTypeId() == rollerShutterOpenActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Opening roller shutter";
            m_simulationTimers.value(thing)->setProperty("targetValue", 0);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(rollerShutterMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == rollerShutterCloseActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Closing roller shutter";
            m_simulationTimers.value(thing)->setProperty("targetValue", 100);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(rollerShutterMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == rollerShutterStopActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Stopping roller shutter";
            m_simulationTimers.value(thing)->stop();
            thing->setStateValue(rollerShutterMovingStateTypeId, false);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == rollerShutterPercentageActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Setting awning to" << action.param(rollerShutterPercentageActionPercentageParamTypeId);
            m_simulationTimers.value(thing)->setProperty("targetValue", action.param(rollerShutterPercentageActionPercentageParamTypeId).value());
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(rollerShutterMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    if (thing->thingClassId() == extendedAwningThingClassId) {
        if (action.actionTypeId() == extendedAwningOpenActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Opening awning";
            m_simulationTimers.value(thing)->setProperty("targetValue", 100);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedAwningMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedAwningCloseActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Closing awning";
            m_simulationTimers.value(thing)->setProperty("targetValue", 0);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedAwningMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedAwningStopActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Stopping awning";
            m_simulationTimers.value(thing)->stop();
            thing->setStateValue(extendedAwningMovingStateTypeId, false);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedAwningPercentageActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Setting awning to" << action.param(extendedAwningPercentageActionPercentageParamTypeId);
            m_simulationTimers.value(thing)->setProperty("targetValue", action.param(extendedAwningPercentageActionPercentageParamTypeId).value());
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedAwningMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    if (thing->thingClassId() == simpleBlindThingClassId) {
        if (action.actionTypeId() == simpleBlindOpenActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Opening simple blind";
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == simpleBlindCloseActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Closing simple blind";
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == simpleBlindStopActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Stopping simple blind";
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    if (thing->thingClassId() == extendedBlindThingClassId) {
        if (action.actionTypeId() == extendedBlindOpenActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Opening extended blind";
            m_simulationTimers.value(thing)->setProperty("targetValue", 0);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedBlindCloseActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Closing extended blind";
            m_simulationTimers.value(thing)->setProperty("targetValue", 100);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedBlindStopActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Stopping extended blind";
            m_simulationTimers.value(thing)->stop();
            thing->setStateValue(extendedBlindMovingStateTypeId, false);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == extendedBlindPercentageActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Setting extended blind to" << action.param(extendedBlindPercentageActionPercentageParamTypeId);
            m_simulationTimers.value(thing)->setProperty("targetValue", action.param(extendedBlindPercentageActionPercentageParamTypeId).value());
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(extendedBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    if (thing->thingClassId() == venetianBlindThingClassId) {
        if (action.actionTypeId() == venetianBlindOpenActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Opening venetian blind";
            m_simulationTimers.value(thing)->setProperty("targetPosition", 0);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(venetianBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == venetianBlindCloseActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Closing venetian blind";
            m_simulationTimers.value(thing)->setProperty("targetPosition", 100);
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(venetianBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == venetianBlindStopActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Stopping venetian blind";
            m_simulationTimers.value(thing)->stop();
            m_simulationTimers.value(thing)->setProperty("targetPosition", thing->stateValue(venetianBlindPercentageStateTypeId).toInt());
            m_simulationTimers.value(thing)->setProperty("targetAngle", thing->stateValue(venetianBlindAngleStateTypeId).toInt());
            thing->setStateValue(venetianBlindMovingStateTypeId, false);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == venetianBlindPercentageActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Setting venetian blind position to" << action.param(venetianBlindPercentageActionPercentageParamTypeId);
            m_simulationTimers.value(thing)->setProperty("targetPosition", action.param(venetianBlindPercentageActionPercentageParamTypeId).value());
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(venetianBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == venetianBlindAngleActionTypeId) {
            qCDebug(dcClosablesSimulation()) << "Setting venetian blind angle to" << action.param(venetianBlindAngleActionAngleParamTypeId);
            m_simulationTimers.value(thing)->setProperty("targetAngle", action.param(venetianBlindAngleActionAngleParamTypeId).value());
            m_simulationTimers.value(thing)->start(500);
            thing->setStateValue(venetianBlindMovingStateTypeId, true);
            return info->finish(Thing::ThingErrorNoError);
        }
    }

    qCWarning(dcClosablesSimulation()) << "Unhandled thing class" << thing->thingClassId() << "for" << thing->name();
}

int IntegrationPluginSimulation::generateRandomIntValue(int min, int max)
{
    int value = ((std::rand() % ((max + 1) - min)) + min);
    // qCDebug(dcSimulation()) << "Generateed random int value: [" << min << ", " << max << "] -->" << value;
    return value;
}

double IntegrationPluginSimulation::generateRandomDoubleValue(double min, double max)
{
    double value = generateRandomIntValue(static_cast<int>(min * 10), static_cast<int>(max * 10)) / 10.0;
    // qCDebug(dcSimulation()) << "Generated random double value: [" << min << ", " << max << "] -->" << value;
    return value;
}

bool IntegrationPluginSimulation::generateRandomBoolValue()
{
    bool value = static_cast<bool>(generateRandomIntValue(0, 1));
    // qCDebug(dcSimulation()) << "Generated random bool value:" << value;
    return value;
}

qreal IntegrationPluginSimulation::generateSinValue(int min, int max, int hourOffset, int decimals)
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

qreal IntegrationPluginSimulation::generateBatteryValue(int chargeStartHour, int chargeDurationInMinutes)
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

qreal IntegrationPluginSimulation::generateNoisyRectangle(int min, int max, int maxNoise, int stablePeriodInMinutes, int &lastValue, QDateTime &lastChangeTimestamp)
{
    QDateTime now = QDateTime::currentDateTime();
    qCDebug(dcClosablesSimulation()) << "Generating noisy rect:" << min << "-" << max << "lastValue:" << lastValue << "lastUpdate" << lastChangeTimestamp << lastChangeTimestamp.secsTo(now) << lastChangeTimestamp.isValid();
    if (!lastChangeTimestamp.isValid() || lastChangeTimestamp.secsTo(now) / 60 > stablePeriodInMinutes) {
        lastChangeTimestamp.swap(now);
        lastValue = min + std::rand() % (max - min);
        qCDebug(dcClosablesSimulation()) << "New last value:" << lastValue;
    }
    qreal noise = 0.1 * (std::rand() % (maxNoise * 20)  - maxNoise);
    qreal ret = 1.0 * lastValue + noise;
    return ret;
}

void IntegrationPluginSimulation::simulationTimerTimeout()
{
    QTimer *t = static_cast<QTimer*>(sender());
    Thing *thing = m_simulationTimers.key(t);
    if (thing->thingClassId() == garageGateThingClassId) {
        if (thing->stateValue(garageGateStateStateTypeId).toString() == "opening") {
            thing->setStateValue(garageGateIntermediatePositionStateTypeId, false);
            thing->setStateValue(garageGateStateStateTypeId, "open");
        }
        if (thing->stateValue(garageGateStateStateTypeId).toString() == "closing") {
            thing->setStateValue(garageGateIntermediatePositionStateTypeId, false);
            thing->setStateValue(garageGateStateStateTypeId, "closed");
        }
    } else if (thing->thingClassId() == extendedAwningThingClassId) {
        int currentValue = thing->stateValue(extendedAwningPercentageStateTypeId).toInt();
        int targetValue = t->property("targetValue").toInt();
        int newValue = targetValue > currentValue ? qMin(targetValue, currentValue + 5) : qMax(targetValue, currentValue - 5);
        thing->setStateValue(extendedAwningPercentageStateTypeId, newValue);
        if (newValue == targetValue) {
            t->stop();
            thing->setStateValue(extendedAwningMovingStateTypeId, false);
        }
    } else if (thing->thingClassId() == extendedBlindThingClassId) {
        int currentValue = thing->stateValue(extendedBlindPercentageStateTypeId).toInt();
        int targetValue = t->property("targetValue").toInt();
        int newValue = targetValue > currentValue ? qMin(targetValue, currentValue + 5) : qMax(targetValue, currentValue - 5);
        thing->setStateValue(extendedBlindPercentageStateTypeId, newValue);
        if (newValue == targetValue) {
            t->stop();
            thing->setStateValue(extendedBlindMovingStateTypeId, false);
        }
    } else if (thing->thingClassId() == venetianBlindThingClassId) {
        int targetPosition = t->property("targetPosition").toInt();
        int targetAngle = t->property("targetAngle").toInt();

        int currentPosition = thing->stateValue(venetianBlindPercentageStateTypeId).toInt();
        int currentAngle = thing->stateValue(venetianBlindAngleStateTypeId).toInt();

        int newPosition = targetPosition > currentPosition ? qMin(targetPosition, currentPosition + 5) : qMax(targetPosition, currentPosition - 5);
        thing->setStateValue(venetianBlindPercentageStateTypeId, newPosition);

        int newAngle = targetAngle > currentAngle ? qMin(targetAngle, currentAngle + 5) : qMax(targetAngle, currentAngle - 5);
        thing->setStateValue(venetianBlindAngleStateTypeId, newAngle);

        if (newPosition == targetPosition && newAngle == targetAngle) {
            t->stop();
            thing->setStateValue(venetianBlindMovingStateTypeId, false);
        }
    } else if (thing->thingClassId() == rollerShutterThingClassId) {
        int currentValue = thing->stateValue(rollerShutterPercentageStateTypeId).toInt();
        int targetValue = t->property("targetValue").toInt();
        int newValue = targetValue > currentValue ? qMin(targetValue, currentValue + 5) : qMax(targetValue, currentValue - 5);
        thing->setStateValue(rollerShutterPercentageStateTypeId, newValue);
        if (newValue == targetValue) {
            t->stop();
            thing->setStateValue(rollerShutterMovingStateTypeId, false);
        }
    }
}
