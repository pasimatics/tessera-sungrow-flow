# Sungrow energy flow

> Beta: checked against the plugin API's headers, but not yet seen on a screen. Expect layout fixes in the first versions.

A plugin for [Tessera](https://github.com/MaxGramser/homeassistant_espscreen) touch screens: the power of a house with
solar panels and a battery on one tile, in the look of the
[Sungrow Energy Flow Card](https://github.com/pasimatics/sungrow-energy-flow-card) for Home Assistant dashboards.

- **The tile** shows solar, grid, house and battery around the inverter, with the power of each. Dots step along the
  lines once a second while power flows, further the more it is. Beside the solar circle stands the power of up to four
  strings, beside the battery what it does and its temperature, and under the house its largest source with its share.
- **A tap** opens a card with the production and consumption per day as bars, for 7, 14 or 30 days. Tap a day for its
  values. Under the bars stand the self-sufficiency and the self-consumption of that day and of the period.

It works with any inverter whose sensors Home Assistant has; the defaults are the entity names of the
[mkaiser Sungrow Modbus integration](https://github.com/mkaiser/Sungrow-SHx-Inverter-Modbus-Home-Assistant).

## Set up

1. In Tessera, open **Plugins**, choose **Add with a link** and paste this repository's link.
2. Add the plugin to a screen. Under **When building**, check the five energy counters (they are filled in with the
   names of the mkaiser integration) and choose **Save and build**. The screen builds once.
3. In **Layout**, place the **Energy flow** tile from the library's Plugins group. It takes at least 2 x 2 cells; the
   whole page gives it the most room.
4. In the inspector, choose the sensors: solar power, grid power, battery power and battery charge. House power, the
   strings and the battery temperature are optional.
5. Check the directions. The tile expects a grid sensor that is positive while the house imports, and a battery sensor
   that is positive while the battery discharges. Turn on **Invert the grid** or **Invert the battery** for a sensor
   that counts the other way.

## Sensors

| Option | What | Example |
|---|---|---|
| Solar power | Current production, W or kW | `sensor.total_dc_power` |
| Grid power | Signed, positive on import | your meter's active power |
| Battery power | Signed, positive on discharge | your battery's signed power |
| Battery charge | State of charge in % | `sensor.battery_level` |
| House power | Optional; worked out without it | `sensor.load_power` |
| String 1 to 4 power | Optional | `sensor.mppt1_power` ... |
| Battery temperature | Optional | `sensor.battery_temperature` |

The counters of the card are chosen once per screen, when the plugin is added:

| Counter | Default |
|---|---|
| Solar production | `sensor.total_pv_generation` |
| Grid import | `sensor.total_imported_energy` |
| Grid export | `sensor.total_exported_energy` |
| Battery charge | `sensor.total_battery_charge` |
| Battery discharge | `sensor.total_battery_discharge` |

Each needs long-term statistics in Home Assistant (`state_class: total` or `total_increasing`).

## Good to know

- A day's consumption is what the house imported, plus what it kept of its own production, plus what the battery gave.
  Self-sufficiency is the part of the consumption that did not come from the grid; self-consumption is the part of the
  production that was not exported.
- The card asks Home Assistant for the days when it opens and keeps the answer for a quarter of an hour. Today's bar
  grows with the hours, as the statistics do.
- A plugin's tile draws once a second, so the dots step instead of gliding as they do on a dashboard.
- The plugin uses nothing outside the home: it reads Home Assistant's sensors and statistics through the Tessera app.
