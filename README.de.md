# Sungrow Energiefluss

> Beta: gegen die Header der Plugin-API geprüft, aber noch nicht auf einem Display gesehen. In den ersten Versionen sind
> Korrekturen am Layout zu erwarten.

Ein Plugin für [Tessera](https://github.com/MaxGramser/homeassistant_espscreen)-Touchdisplays: die Leistung eines Hauses
mit Solaranlage und Batterie auf einer Kachel, im Stil der
[Sungrow Energy Flow Card](https://github.com/pasimatics/sungrow-energy-flow-card) für Home-Assistant-Dashboards.

- **Die Kachel** zeigt Solar, Netz, Haus und Batterie rund um den Wechselrichter, jeweils mit ihrer Leistung. Solange
  Leistung fließt, wandern Punkte im Sekundentakt über die Linien, bei mehr Leistung in größeren Schritten. Neben dem
  Solarkreis steht die Leistung von bis zu vier Strings, neben der Batterie ihr Zustand und ihre Temperatur, unter dem
  Haus die größte Quelle mit ihrem Anteil.
- **Ein Tipp** öffnet eine Karte mit Erzeugung und Verbrauch pro Tag als Balken, für 7, 14 oder 30 Tage. Ein Tipp auf
  einen Tag zeigt dessen Werte. Unter den Balken stehen Autarkie und Eigenverbrauch des Tages und des Zeitraums.

Es funktioniert mit jedem Wechselrichter, dessen Sensoren Home Assistant kennt; voreingestellt sind die Entitätsnamen
der [mkaiser-Sungrow-Modbus-Integration](https://github.com/mkaiser/Sungrow-SHx-Inverter-Modbus-Home-Assistant).

## Einrichten

1. In Tessera **Plugins** öffnen, **Add with a link** wählen und den Link dieses Repositorys einfügen.
2. Das Plugin einem Screen hinzufügen. Unter **When building** die fünf Energiezähler prüfen (vorbelegt mit den Namen
   der mkaiser-Integration) und **Save and build** wählen. Der Screen wird einmal gebaut.
3. Unter **Layout** die Kachel **Energiefluss** aus der Gruppe Plugins platzieren. Sie braucht mindestens 2 x 2 Zellen;
   auf der ganzen Seite hat sie am meisten Platz.
4. Im Inspektor die Sensoren wählen: Solarleistung, Netzleistung, Batterieleistung und Batterieladestand.
   Hausverbrauch, Strings und Batterietemperatur sind optional.
5. Die Richtungen prüfen. Die Kachel erwartet einen Netzsensor, der bei Bezug positiv ist, und einen Batteriesensor,
   der beim Entladen positiv ist. Für Sensoren mit umgekehrtem Vorzeichen **Netz umkehren** oder **Batterie umkehren**
   einschalten.

## Gut zu wissen

- Der Verbrauch eines Tages ist der Netzbezug, plus der selbst behaltene Teil der Erzeugung, plus das, was die Batterie
  abgegeben hat. Autarkie ist der Teil des Verbrauchs, der nicht aus dem Netz kam; Eigenverbrauch ist der Teil der
  Erzeugung, der nicht eingespeist wurde.
- Die Karte fragt Home Assistant beim Öffnen nach den Tagen und behält die Antwort eine Viertelstunde. Der Balken von
  heute wächst mit den Stunden, wie die Statistik.
- Eine Plugin-Kachel zeichnet einmal pro Sekunde, deshalb springen die Punkte, statt wie im Dashboard zu gleiten.
- Das Plugin nutzt nichts außerhalb des Hauses: Es liest Sensoren und Statistik von Home Assistant über die Tessera-App.
