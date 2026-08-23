#pragma once

#include <Arduino.h>
#include <ir_Daikin.h>

#include "../../api/Json.h"
#include "../../ir/IrSender.h"

enum class DaikinStateSource : uint8_t {
  Initial,
  Transmitted,
  Received,
};

class DaikinAircon {
 public:
  explicit DaikinAircon(IrSender &ir);

  void begin();
  IrSendResult sendOff();
  IrSendResult applyPatch(const HomeJson::Object &object, String &errorCode,
                          String &errorMessage);
  IrSendResult replay();
  bool handleReceived(const uint8_t *state, size_t length,
                      uint32_t receivedAtMs);
  String stateJson() const;
  String receivedJson() const;

 private:
  static const char *modeName(uint8_t mode);
  static const char *stateSourceName(DaikinStateSource source);
  static String fanName(uint8_t fan);

  bool parseTimer(const String &json, bool &hasOn, bool &onEnabled,
                  uint16_t &onMinutes, bool &hasOff, bool &offEnabled,
                  uint16_t &offMinutes, String &errorCode,
                  String &errorMessage) const;
  uint16_t timerClockMinutes() const;
  void syncTimerClock(uint16_t currentMinutes);
  uint16_t timerTargetFromDuration(uint16_t currentMinutes,
                                   uint16_t durationMinutes) const;
  uint16_t timerDurationFromTarget(bool enabled, uint16_t targetMinutes) const;
  void syncNormalTemperatureFromAc();
  IrSendResult sendCurrentState();
  void setArcSleep(bool enabled);
  void setArcHealth(bool enabled);
  void setArcClean(bool enabled);
  bool arcSleep() const;
  bool arcHealth() const;
  bool arcClean() const;

  IrSender &ir_;
  IRDaikinESP &ac_;
  bool power_;
  uint8_t mode_;
  uint8_t normalTemperature_;
  uint8_t fan_;
  DaikinStateSource stateSource_;
  bool hasReceivedState_;
  uint8_t lastReceivedState_[kDaikinStateLength];
  uint32_t lastReceivedAtMs_;
  bool timerClockKnown_;
  uint16_t timerClockBaseMinutes_;
  uint32_t timerClockBaseAtMs_;
};
