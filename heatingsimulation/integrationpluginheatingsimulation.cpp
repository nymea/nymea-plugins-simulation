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

#include "integrationpluginheatingsimulation.h"
#include "plugininfo.h"

#include "plugintimer.h"

IntegrationPluginHeatingSimulation::IntegrationPluginHeatingSimulation(QObject *parent): IntegrationPlugin (parent)
{

}

IntegrationPluginHeatingSimulation::~IntegrationPluginHeatingSimulation()
{
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer20Seconds);
    hardwareManager()->pluginTimerManager()->unregisterTimer(m_pluginTimer1Min);
}

void IntegrationPluginHeatingSimulation::init()
{
    // Change some values every 20 seconds
    m_pluginTimer20Seconds = hardwareManager()->pluginTimerManager()->registerTimer(20);
    connect(m_pluginTimer20Seconds, &PluginTimer::timeout, this, &IntegrationPluginHeatingSimulation::onPluginTimer20Seconds);

    // Change some values every min
    m_pluginTimer1Min = hardwareManager()->pluginTimerManager()->registerTimer(300);
    connect(m_pluginTimer1Min, &PluginTimer::timeout, this, &IntegrationPluginHeatingSimulation::onPluginTimer1Minute);
}

void IntegrationPluginHeatingSimulation::startMonitoringAutoThings()
{
}

void IntegrationPluginHeatingSimulation::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (thing->thingClassId() == thermostatThingClassId) {
    }

    if (thing->thingClassId() == x2wpThingClassId) {
        thing->setStateValue(x2wpConnectedStateTypeId, true);
        thing->setStateValue(x2wpPowerStateTypeId, true);

    } else if (thing->thingClassId() == x2luThingClassId) {
        thing->setStateValue(x2luConnectedStateTypeId, true);
    }

    info->finish(Thing::ThingErrorNoError);
}


void IntegrationPluginHeatingSimulation::thingRemoved(Thing *thing)
{
    Q_UNUSED(thing)

    if (m_simulationTimers.contains(thing)) {
        QTimer *t = m_simulationTimers.take(thing);
        t->stop();
        t->deleteLater();
    }
}

void IntegrationPluginHeatingSimulation::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();

    if (thing->thingClassId() == heatingThingClassId) {

        // check if this is the "set power" action
        if (action.actionTypeId() == heatingPowerActionTypeId) {

            // get the param value
            Param powerParam = action.param(heatingPowerActionPowerParamTypeId);
            bool power = powerParam.value().toBool();
            qCDebug(dcHeatingSimulation()) << "Set power" << power << "for heating device" << thing->name();
            thing->setStateValue(heatingPowerStateTypeId, power);
            return info->finish(Thing::ThingErrorNoError);

        } else if (action.actionTypeId() == heatingPercentageActionTypeId) {

            // get the param value
            Param percentageParam = action.param(heatingPercentageActionPercentageParamTypeId);
            int percentage = percentageParam.value().toInt();

            qCDebug(dcHeatingSimulation()) << "Set target temperature percentage" << percentage << "for heating device" << thing->name();

            thing->setStateValue(heatingPercentageStateTypeId, percentage);
            return info->finish(Thing::ThingErrorNoError);
        }
        return info->finish(Thing::ThingErrorActionTypeNotFound);

    } else if (thing->thingClassId() == thermostatThingClassId) {
        if (action.actionTypeId() == thermostatBoostActionTypeId) {
            bool boost = action.param(thermostatBoostActionBoostParamTypeId).value().toBool();
            qCDebug(dcHeatingSimulation()) << "Set boost" << boost << "for thermostat device" << thing->name();
            thing->setStateValue(thermostatBoostStateTypeId, boost);
            QTimer *t = m_simulationTimers.value(thing);
            if (!t) {
                t = new QTimer(thing);
                m_simulationTimers.insert(thing, t);
            }
            t->setInterval(5 * 60 * 1000);
            t->setSingleShot(true);
            connect(t, &QTimer::timeout, t, &QTimer::deleteLater);
            connect(t, &QTimer::timeout, thing, [thing](){
                thing->setStateValue(thermostatBoostStateTypeId, false);
            });
            return info->finish(Thing::ThingErrorNoError);
        }
        if (action.actionTypeId() == thermostatTargetTemperatureActionTypeId) {
            double targetTemp = action.param(thermostatTargetTemperatureActionTargetTemperatureParamTypeId).value().toDouble();
            qCDebug(dcHeatingSimulation()) << "Set targetTemp" << targetTemp << "for thermostat device" << thing->name();
            thing->setStateValue(thermostatTargetTemperatureStateTypeId, targetTemp);
            return info->finish(Thing::ThingErrorNoError);
        }

    } else if (action.actionTypeId() == x2luVentilationModeActionTypeId) {
        QString mode = action.param(x2luVentilationModeActionVentilationModeParamTypeId).value().toString();
        qCDebug(dcHeatingSimulation()) << "ExecuteAction" << action.actionTypeId() << mode;
        thing->setStateValue(x2luVentilationModeStateTypeId, mode);
        if (mode == "Manual level 0") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 0);
        } else if (mode == "Manual level 1") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 1);
        } else if (mode == "Manual level 2") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 2);
        } else if (mode == "Manual level 3") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 3);
        } else if (mode == "Automatic") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 1);
        } else if (mode == "Party") {
            thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 3);
        }
    } else if (action.actionTypeId() == x2wpPowerActionTypeId) {
        thing->setStateValue(x2wpPowerStateTypeId, action.param(x2wpPowerActionPowerParamTypeId).value());
    } else if (action.actionTypeId() == x2wpTargetTemperatureActionTypeId) {
        thing->setStateValue(x2wpTargetTemperatureStateTypeId, action.param(x2wpTargetTemperatureActionTargetTemperatureParamTypeId).value());
    } else if (action.actionTypeId() == x2wpTargetWaterTemperatureActionTypeId) {
        thing->setStateValue(x2wpTargetTemperatureStateTypeId, action.param(x2wpTargetWaterTemperatureActionTargetWaterTemperatureParamTypeId).value());
    }
    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginHeatingSimulation::onPluginTimer1Minute()
{
    foreach (Thing *thing, myThings()) {
        if(thing->thingClassId() == x2wpThingClassId) {
            double targetValue = thing->stateValue(x2wpTargetTemperatureStateTypeId).toDouble();
            double currentValue = thing->stateValue(x2wpTemperatureStateTypeId).toDouble();
            int r = qrand();
            int diff = qRound(qAbs(targetValue - currentValue) + 1);
            int maxDelta = diff + 1;
            double delta = (r % maxDelta) * 0.1;
            double downCorrection = diff * .025;
            delta = delta - downCorrection;
            if (targetValue > currentValue) {
                thing->setStateValue(x2wpTemperatureStateTypeId, currentValue + delta);
            } else {
                thing->setStateValue(x2wpTemperatureStateTypeId, currentValue - delta);
            }

        } else if (thing->thingClassId() == x2luThingClassId) {
            int ventilationLevel = thing->stateValue(x2luActiveVentilationLevelStateTypeId).toInt();
            double targetValue = ventilationLevel > 0 ? 350 : 1500;
            double currentValue = thing->stateValue(x2luCo2StateTypeId).toDouble();
            int r = qrand();
            int diff = qRound(qAbs(targetValue - currentValue) + 1);
            int maxDelta = diff + 1;
            double delta = (r % maxDelta) * (0.01 * (ventilationLevel + 1));
            double downCorrection = diff * .0025;
            delta = delta - downCorrection;
            if (targetValue > currentValue) {
                thing->setStateValue(x2luCo2StateTypeId, currentValue + delta);
            } else {
                thing->setStateValue(x2luCo2StateTypeId, currentValue - (delta * 2));
            }

            bool autoVentilation = thing->stateValue(x2luVentilationModeStateTypeId).toString() == "Automatic";
            if (autoVentilation) {
                if (ventilationLevel == 0 && currentValue > 800) {
                    thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 1);
                } else if (ventilationLevel >= 1 && currentValue < 400) {
                    thing->setStateValue(x2luActiveVentilationLevelStateTypeId, 0);
                }
            }
        }
    }
}

void IntegrationPluginHeatingSimulation::onPluginTimer20Seconds()
{
    foreach (Thing *thing, myThings()) {
        if (thing->thingClassId() == thermostatThingClassId) {
            double targetTemp = thing->stateValue(thermostatTargetTemperatureStateTypeId).toDouble();
            double currentTemp = thing->stateValue(thermostatTemperatureStateTypeId).toDouble();
            bool heatingOn = thing->stateValue(thermostatHeatingOnStateTypeId).toBool();
            bool coolingOn = thing->stateValue(thermostatCoolingOnStateTypeId).toBool();
            bool boost = thing->stateValue(thermostatBoostStateTypeId).toBool();

            // When we're heating, temp increases slowly until it's up on par with target temp
            if (heatingOn) {
                double diff = targetTemp - currentTemp;
                currentTemp += 0.005 + diff * (boost ? 0.2 : 0.1);
                if (currentTemp >= targetTemp) {
                    thing->setStateValue(thermostatHeatingOnStateTypeId, false);
                }
            } else {
                // Decrease 1% per interval to simulate drop of temperature (assuming it's cold outside)
                currentTemp = currentTemp * 0.995;

                // Start heating when we're more than 2 degrees lower than what we should be
                if (currentTemp < targetTemp - 2) {
                    thing->setStateValue(thermostatHeatingOnStateTypeId, true);
                }
            }

            if (coolingOn) {
                double diff = targetTemp - currentTemp;
                currentTemp += diff * 0.1;
                if (currentTemp <= targetTemp) {
                    thing->setStateValue(thermostatCoolingOnStateTypeId, false);
                }
            } else {
                if (currentTemp > targetTemp + 2) {
                    thing->setStateValue(thermostatCoolingOnStateTypeId, true);
                }
            }

            thing->setStateValue(thermostatTemperatureStateTypeId, currentTemp);
        }
    }
}

void IntegrationPluginHeatingSimulation::simulationTimerTimeout()
{
    QTimer *t = static_cast<QTimer*>(sender());
    Thing *thing = m_simulationTimers.key(t);
    if (thing->thingClassId() == thermostatThingClassId) {
        thing->setStateValue(thermostatBoostStateTypeId, false);
        t->stop();
    }
}
