// SPDX-License-Identifier: GPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2025, chargebyte austria GmbH
*
* This file is part of nymea-plugins-simulation.
*
* nymea-plugins-simulation is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* nymea-plugins-simulation is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nymea-plugins-simulation. If not, see <https://www.gnu.org/licenses/>.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef INTEGRATIONPLUGINSIMMULATION_H
#define INTEGRATIONPLUGINSIMMULATION_H

#include "integrations/integrationplugin.h"
#include "plugintimer.h"

#include <QDateTime>

#include "extern-plugininfo.h"

class IntegrationPluginAppliancesSimulation : public IntegrationPlugin
{
    Q_OBJECT

    Q_PLUGIN_METADATA(IID "io.nymea.IntegrationPlugin" FILE "integrationpluginappliancessimulation.json")
    Q_INTERFACES(IntegrationPlugin)


public:
    explicit IntegrationPluginAppliancesSimulation();
    ~IntegrationPluginAppliancesSimulation();

    void init() override;
    void setupThing(ThingSetupInfo *info) override;
    void thingRemoved(Thing *thing) override;
    void executeAction(ThingActionInfo *info) override;

private:
    PluginTimer *m_pluginTimer20Seconds = nullptr;

    int generateRandomIntValue(int min, int max);
    double generateRandomDoubleValue(double min, double max);
    bool generateRandomBoolValue();

    // Generates values in a sin curve from min to max, moving the start by hourOffset from midnight
    qreal generateSinValue(int min, int max, int hourOffset, int decimals = 2);
    qreal generateBatteryValue(int chargeStartHour, int chargeDurationInMinutes);

    QHash<Thing*, QTimer*> m_simulationTimers;
private slots:
    void onPluginTimer20Seconds();
    void simulationTimerTimeout();

};

#endif // INTEGRATIONPLUGINSIMMULATION_H
