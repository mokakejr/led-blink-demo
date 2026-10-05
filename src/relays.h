#pragma once
// Pilote de 2 sorties relais (contacts secs). Pour une autre cible (série, BLE...),
// c'est CE fichier seul qu'il faut réécrire (garde la même API).
#include <Arduino.h>
#include "esp_timer.h"
#include "config.h"

enum class Dir : uint8_t { NONE, UP, DOWN };

class Relays {
public:
  void begin() {
    _mtx = xSemaphoreCreateMutex();
    pinMode(PIN_UP, OUTPUT);
    pinMode(PIN_DOWN, OUTPUT);
    release();
    esp_timer_create_args_t args = {};
    args.callback = &Relays::onTimer;
    args.arg = this;
    args.name = "relay_stop";
    esp_timer_create(&args, &_timer);
  }

  // Lance un mouvement. Retourne la durée réellement appliquée (ms).
  // L'arrêt est assuré par un timer matériel : même si le Wi-Fi ou
  // le réseau plante, le relais est relâché à l'échéance.
  uint32_t move(Dir d, uint32_t ms) {
    const uint32_t maxMs = MAX_MOVE_S * 1000UL;
    if (ms > maxMs) ms = maxMs;
    if (ms == 0 || d == Dir::NONE) return 0;
    Lock lock(_mtx);
    bool reverse = (_dir != Dir::NONE && _dir != d);
    esp_timer_stop(_timer);
    release();
    if (reverse) delay(300);  // temps mort avant d'inverser le sens
    _startMs = millis();
    _dir = d;
    setPin(d == Dir::UP ? PIN_UP : PIN_DOWN, true);
    esp_timer_start_once(_timer, (uint64_t)ms * 1000ULL);
    return ms;
  }

  // Arrête tout. Retourne la durée écoulée (ms), 0 si déjà à l'arrêt.
  uint32_t stop() {
    Lock lock(_mtx);
    if (_dir == Dir::NONE) return 0;
    uint32_t elapsed = millis() - _startMs;
    esp_timer_stop(_timer);
    release();
    return elapsed;
  }

  bool moving() const { return _dir != Dir::NONE; }
  Dir dir() const { return _dir; }

private:
  struct Lock {
    SemaphoreHandle_t m;
    explicit Lock(SemaphoreHandle_t mm) : m(mm) { xSemaphoreTake(m, portMAX_DELAY); }
    ~Lock() { xSemaphoreGive(m); }
  };

  esp_timer_handle_t _timer = nullptr;
  SemaphoreHandle_t _mtx = nullptr;
  volatile Dir _dir = Dir::NONE;
  volatile uint32_t _startMs = 0;

  void setPin(int pin, bool on) {
    digitalWrite(pin, (on == (bool)RELAY_ACTIVE_HIGH) ? HIGH : LOW);
  }
  void release() {
    setPin(PIN_UP, false);
    setPin(PIN_DOWN, false);
    _dir = Dir::NONE;
  }
  static void onTimer(void* arg) { static_cast<Relays*>(arg)->release(); }
};
