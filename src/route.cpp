#include "route.h"

#include <string.h>

int stationIndexByName(const char* name) {
  if (!name || !*name) return -1;
  for (int i = 0; i < kStationCount; i++) {
    if (strcmp(kStations[i].name, name) == 0) return i;
  }
  return -1;
}

int stationIndexByBase(uint16_t base) {
  for (int i = 0; i < kStationCount; i++) {
    if (kStations[i].base == base) return i;
  }
  return -1;
}

Direction routeDirection(int originIdx, int destIdx) {
  return destIdx > originIdx ? DIR_SOUTH : DIR_NORTH;
}

uint32_t stationStopId(int stationIdx, Direction dir) {
  if (stationIdx < 0 || stationIdx >= kStationCount) return 0;
  return (uint32_t)kStations[stationIdx].base * 10u + (dir == DIR_NORTH ? 1u : 2u);
}

bool routeValid(int originIdx, int destIdx) {
  if (originIdx < 0 || originIdx >= kStationCount) return false;
  if (destIdx < 0 || destIdx >= kStationCount) return false;
  return originIdx != destIdx;
}

