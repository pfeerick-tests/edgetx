/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x 
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"

void parseTelemHubByte(uint8_t byte)
{
  static int8_t structPos;
  static uint8_t lowByte;
  static TS_STATE state = TS_IDLE;

  if (byte == 0x5e) {
    state = TS_DATA_ID;
    return;
  }
  if (state == TS_IDLE) {
    return;
  }
  if (state & TS_XOR) {
    byte = byte ^ 0x60;
    state = (TS_STATE)(state - TS_XOR);
  }
  else if (byte == 0x5d) {
    state = (TS_STATE)(state | TS_XOR);
    return;
  }
  if (state == TS_DATA_ID) {
    if (byte > 0x3f) {
      state = TS_IDLE;
    }
    else {
      structPos = byte;
      state = TS_DATA_LOW;
    }
    return;
  }
  if (state == TS_DATA_LOW) {
    lowByte = byte;
    state = TS_DATA_HIGH;
    return;
  }

  state = TS_IDLE;
  processHubPacket(structPos, (byte << 8) + lowByte);
}

void frskyDProcessPacket(const uint8_t *packet)
{
  // What type of packet?
  switch (packet[0])
  {
    case LINKPKT: // A1/A2/RSSI values
    {
      setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, D_A1_ID, 0, 0, packet[1], UNIT_VOLTS, 0);
      setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, D_A2_ID, 0, 0, packet[2], UNIT_VOLTS, 0);
      setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, D_RSSI_ID, 0, 0, packet[3], UNIT_RAW, 0);
#if defined(MULTIMODULE)
      if (telemetryProtocol == PROTOCOL_TELEMETRY_MULTIMODULE) {
        setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, TX_RSSI_ID, 0, 0, packet[4]>>1, UNIT_DB,  0);
        setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, RX_LQI_ID,  0, 0, packet[5]   , UNIT_RAW, 0);
        setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, TX_LQI_ID , 0, 0, packet[6]   , UNIT_RAW, 0);
      }
#endif
      telemetryData.rssi.set(packet[3]);
      telemetryStreaming = TELEMETRY_TIMEOUT10ms; // reset counter only if valid packets are being detected
      break;
    }

    case USRPKT: // User Data packet
      uint8_t numBytes = 3 + (packet[1] & 0x07); // sanitize in case of data corruption leading to buffer overflow
      for (uint8_t i=3; i<numBytes; i++) {
        parseTelemHubByte(packet[i]);
      }
      break;
  }
}

struct FrSkyDSensor {
  const uint8_t id;
  const char * name;
  const TelemetryUnit unit;
  const uint8_t prec;
};

const FrSkyDSensor frskyDSensors[] = {
  { D_RSSI_ID, STR_SENSOR_RSSI, UNIT_RAW, 0 },
  { D_A1_ID, STR_SENSOR_A1, UNIT_VOLTS, 1 },
  { D_A2_ID, STR_SENSOR_A2, UNIT_VOLTS, 1 },
  { RPM_ID, STR_SENSOR_RPM, UNIT_RPMS, 0 },
  { FUEL_ID, STR_SENSOR_FUEL, UNIT_PERCENT, 0 },
  { TEMP1_ID, STR_SENSOR_TEMP1, UNIT_CELSIUS, 0 },
  { TEMP2_ID, STR_SENSOR_TEMP2, UNIT_CELSIUS, 0 },
  { CURRENT_ID, STR_SENSOR_CURR, UNIT_AMPS, 1 },
  { ACCEL_X_ID, STR_SENSOR_ACCX, UNIT_G, 3 },
  { ACCEL_Y_ID, STR_SENSOR_ACCY, UNIT_G, 3 },
  { ACCEL_Z_ID, STR_SENSOR_ACCZ, UNIT_G, 3 },
  { VARIO_ID, STR_SENSOR_VSPD, UNIT_METERS_PER_SECOND, 2 },
  { VFAS_ID, STR_SENSOR_VFAS, UNIT_VOLTS, 2 },
  { BARO_ALT_AP_ID, STR_SENSOR_ALT, UNIT_METERS, 1 },   // we map hi precision vario into PREC1!
  { VOLTS_AP_ID, STR_SENSOR_VFAS, UNIT_VOLTS, 2 },
  { GPS_SPEED_BP_ID, STR_SENSOR_GSPD, UNIT_KTS, 0 },
  { GPS_COURS_BP_ID, STR_SENSOR_HDG, UNIT_DEGREE, 0 },
  { VOLTS_ID, STR_SENSOR_CELLS, UNIT_CELLS, 2 },
  { GPS_ALT_BP_ID, STR_SENSOR_GPSALT, UNIT_METERS, 0 },
  { GPS_HOUR_MIN_ID, STR_SENSOR_GPSDATETIME, UNIT_DATETIME, 0 },
  { GPS_LAT_AP_ID, STR_SENSOR_GPS, UNIT_GPS, 0 },
  { 0, NULL, UNIT_RAW, 0 } // sentinel
};

const FrSkyDSensor * getFrSkyDSensor(uint8_t id)
{
  const FrSkyDSensor * result = NULL;
  for (const FrSkyDSensor * sensor = frskyDSensors; sensor->id; sensor++) {
    if (id == sensor->id) {
      result = sensor;
      break;
    }
  }
  return result;
}

uint8_t lastId = 0;
uint16_t lastBPValue = 0;
uint16_t lastAPValue = 0;

int32_t getFrSkyDProtocolGPSValue(int32_t sign)
{
  div_t qr = div(lastBPValue, 100);
  return sign * (((uint32_t) (qr.quot) * 1000000) + (((uint32_t) (qr.rem) * 10000 + lastAPValue) * 5) / 3);
}

void processHubPacket(uint8_t id, int16_t value)
{
  TelemetryUnit unit = UNIT_RAW;
  uint8_t precision = 0;
  int32_t data = value;

  if (id > FRSKY_LAST_ID || id == GPS_SPEED_AP_ID || id == GPS_ALT_AP_ID || id == GPS_COURS_AP_ID) {
    return;
  }

  if (id == GPS_LAT_BP_ID || id == GPS_LONG_BP_ID || id == BARO_ALT_BP_ID || id == VOLTS_BP_ID) {
    lastId = id;
    lastBPValue = value;
    return;
  }

  if (id == GPS_LAT_AP_ID) {
    if (lastId == GPS_LAT_BP_ID) {
      lastId = id;
      lastAPValue = data;
    }
    return;
  }
  else if (id == GPS_LONG_AP_ID) {
    if (lastId == GPS_LONG_BP_ID) {
      lastId = id;
      lastAPValue = data;
    }
    return;
  }
  else if (id == GPS_LAT_NS_ID) {
    if (lastId == GPS_LAT_AP_ID) {
      id = GPS_LAT_AP_ID;
      unit = UNIT_GPS_LATITUDE;
      data = getFrSkyDProtocolGPSValue(value == 'N' ? 1 : -1);
    }
    else {
      return;
    }
  }
  else if (id == GPS_LONG_EW_ID) {
    if (lastId == GPS_LONG_AP_ID) {
      id = GPS_LAT_AP_ID;
      unit = UNIT_GPS_LONGITUDE;
      data = getFrSkyDProtocolGPSValue(value == 'E' ? 1 : -1);
    }
    else {
      return;
    }
  }
  else if (id == BARO_ALT_AP_ID) {
    if (lastId == BARO_ALT_BP_ID) {
      if (data > 9 || telemetryData.varioHighPrecision) {
        telemetryData.varioHighPrecision = true;
        data /= 10;    // map hi precision vario into low precision. Altitude is stored in 0.1m anyways
      }
      data = (int16_t)lastBPValue * 10 + (((int16_t)lastBPValue < 0) ? -data : data);
      unit = UNIT_METERS;
      precision = 1;
    }
    else {
      return;
    }
  }
  else if (id == VOLTS_AP_ID) {
    if (lastId == VOLTS_BP_ID) {
#if defined(FAS_PROTOTYPE)
      data = lastBPValue * 100 + value * 10;
#else
      data = ((lastBPValue * 100 + value * 10) * 210) / 110;
#endif
      unit = UNIT_VOLTS;
      precision = 2;
    }
    else {
      return;
    }
  }
  else if (id == VOLTS_ID) {
    unit = UNIT_CELLS;
    uint32_t cellData = (uint32_t)data;
    if (((cellData & 0x00F0) >> 4) >= MAX_CELLS)
      return;
    data = ((cellData & 0x00F0) << 12) + (((((cellData & 0xFF00) >> 8) + ((cellData & 0x000F) << 8))) / 5);
  }
  else if (id == GPS_DAY_MONTH_ID) {
    id = GPS_HOUR_MIN_ID;
    unit = UNIT_DATETIME_DAY_MONTH;
  }
  else if (id == GPS_HOUR_MIN_ID) {
    unit = UNIT_DATETIME_HOUR_MIN;
  }
  else if (id == GPS_SEC_ID) {
    id = GPS_HOUR_MIN_ID;
    unit = UNIT_DATETIME_SEC;
  }
  else if (id == GPS_YEAR_ID) {
    id = GPS_HOUR_MIN_ID;
    unit = UNIT_DATETIME_YEAR;
  }
  else {
    const FrSkyDSensor * sensor = getFrSkyDSensor(id);
    if (sensor) {
      unit = sensor->unit;
      precision = sensor->prec;
    }
  }
  if (id == RPM_ID) {
    data = data * 60;
  }
  else if (id == VFAS_ID) {
    if (data >= VFAS_D_HIPREC_OFFSET) {
      // incoming value has a resolution of 0.01V and added offset of VFAS_D_HIPREC_OFFSET
      data -= VFAS_D_HIPREC_OFFSET;
    }
    else {
      // incoming value has a resolution of 0.1V
      data *= 10;
    }
  }

  setTelemetryValue(PROTOCOL_TELEMETRY_FRSKY_D, id, 0, 0, data, unit, precision);
}

void frskyDSetDefault(int index, uint16_t id)
{
  TelemetrySensor &telemetrySensor = g_model.telemetrySensors[index];

  telemetrySensor.id = id;
  telemetrySensor.instance = 0;

#if defined(MULTIMODULE)
  if (id == TX_RSSI_ID) {
    telemetrySensor.init(STR_SENSOR_TX_RSSI, UNIT_DB, 0);
    telemetrySensor.filter = 1;
  }
  else if (id == TX_LQI_ID) {
    telemetrySensor.init(STR_SENSOR_TX_QUALITY, UNIT_RAW, 0);
    telemetrySensor.filter = 1;
  }
  else if (id == RX_LQI_ID) {
    telemetrySensor.init(STR_SENSOR_RX_QUALITY, UNIT_RAW, 0);
    telemetrySensor.filter = 1;
  }
  else
#endif
  {
    const FrSkyDSensor * sensor = getFrSkyDSensor(id);
    if (sensor) {
      TelemetryUnit unit = sensor->unit;
      uint8_t prec = min<uint8_t>(2, sensor->prec);
      telemetrySensor.init(sensor->name, unit, prec);
      if (id == D_RSSI_ID) {
        telemetrySensor.filter = 1;
        telemetrySensor.logs = true;
      }
      else if (id >= D_A1_ID && id <= D_A2_ID) {
        telemetrySensor.custom.ratio = 132;
        telemetrySensor.filter = 1;
      }
      else if (id == CURRENT_ID) {
        telemetrySensor.onlyPositive = 1;
      }
      else if (id == BARO_ALT_AP_ID) {
        telemetrySensor.autoOffset = 1;
      }
      if (unit == UNIT_RPMS) {
        telemetrySensor.custom.ratio = 1;
        telemetrySensor.custom.offset = 1;
      }
      else if (unit == UNIT_METERS) {
        if (IS_IMPERIAL_ENABLE()) {
          telemetrySensor.unit = UNIT_FEET;
        }
      }
    }
    else {
      telemetrySensor.init(id);
    }
  }

  storageDirty(EE_MODEL);
}
