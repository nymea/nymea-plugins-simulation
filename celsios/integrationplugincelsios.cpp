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

#include "integrationplugincelsios.h"
#include "plugininfo.h"

#include "plugintimer.h"

ThingPluginCelsios::ThingPluginCelsios(QObject *parent): IntegrationPlugin (parent)
{

}

void ThingPluginCelsios::startMonitoringAutoThings()
{
    if (myThings().isEmpty()) {
        ThingDescriptor vu(x2luThingClassId, "Celsi°s ventilation unit");
        ThingDescriptor hu(x2wpThingClassId, "Celsi°s heating unit");
        emit autoThingsAppeared({hu, vu});
    }
}

void ThingPluginCelsios::setupThing(ThingSetupInfo *info)
{
    Thing *thing = info->thing();

    if (thing->thingClassId() == x2wpThingClassId) {
        thing->setStateValue(x2wpConnectedStateTypeId, true);
        thing->setStateValue(x2wpPowerStateTypeId, true);

    } else if (thing->thingClassId() == x2luThingClassId) {
        thing->setStateValue(x2luConnectedStateTypeId, true);
    }

    if (!m_timer) {
        m_timer = hardwareManager()->pluginTimerManager()->registerTimer(60);
        connect(m_timer, &PluginTimer::timeout, this, [this](){
            foreach (Thing *d, myThings().filterByThingClassId(x2wpThingClassId)) {
                double targetValue = d->stateValue(x2wpTargetTemperatureStateTypeId).toDouble();
                double currentValue = d->stateValue(x2wpTemperatureStateTypeId).toDouble();
                int r = qrand();
                int diff = qRound(qAbs(targetValue - currentValue) + 1);
                int maxDelta = diff + 1;
                double delta = (r % maxDelta) * 0.1;
                double downCorrection = diff * .025;
                delta = delta - downCorrection;
                if (targetValue > currentValue) {
                    d->setStateValue(x2wpTemperatureStateTypeId, currentValue + delta);
                } else {
                    d->setStateValue(x2wpTemperatureStateTypeId, currentValue - delta);
                }
            }
            foreach (Thing *d, myThings().filterByThingClassId(x2luThingClassId)) {
                int ventilationLevel = d->stateValue(x2luActiveVentilationLevelStateTypeId).toInt();
                double targetValue = ventilationLevel > 0 ? 350 : 1500;
                double currentValue = d->stateValue(x2luCo2StateTypeId).toDouble();
                int r = qrand();
                int diff = qRound(qAbs(targetValue - currentValue) + 1);
                int maxDelta = diff + 1;
                double delta = (r % maxDelta) * (0.01 * (ventilationLevel + 1));
                double downCorrection = diff * .0025;
                delta = delta - downCorrection;
                if (targetValue > currentValue) {
                    d->setStateValue(x2luCo2StateTypeId, currentValue + delta);
                } else {
                    d->setStateValue(x2luCo2StateTypeId, currentValue - (delta * 2));
                }

                bool autoVentilation = d->stateValue(x2luVentilationModeStateTypeId).toString() == "Automatic";
                if (autoVentilation) {
                    if (ventilationLevel == 0 && currentValue > 800) {
                        d->setStateValue(x2luActiveVentilationLevelStateTypeId, 1);
                    } else if (ventilationLevel >= 1 && currentValue < 400) {
                        d->setStateValue(x2luActiveVentilationLevelStateTypeId, 0);
                    }
                }
            }
        });
    }
    info->finish(Thing::ThingErrorNoError);
}


void ThingPluginCelsios::thingRemoved(Thing *thing)
{
    Q_UNUSED(thing)
    if (myThings().isEmpty()) {
        hardwareManager()->pluginTimerManager()->unregisterTimer(m_timer);
        m_timer = nullptr;
    }
}

void ThingPluginCelsios::executeAction(ThingActionInfo *info)
{
    Thing *thing = info->thing();
    Action action = info->action();

    if (action.actionTypeId() == x2luVentilationModeActionTypeId) {
        QString mode = action.param(x2luVentilationModeActionVentilationModeParamTypeId).value().toString();
        qCDebug(dcCelsios()) << "ExecuteAction" << action.actionTypeId() << mode;
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
