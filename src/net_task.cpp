#ifdef ARDUINO
#include "net_task.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <string.h>

namespace net_task {
namespace {

// The TLS handshake is the stack-hungry part: mbedtls builds its working state
// there on top of whatever HTTPClient is using. 12 KB has been comfortable in
// practice, and stackHeadroom() reports the real high-water mark on every fetch
// so this number is checked against measurement rather than trusted.
constexpr uint32_t TASK_STACK = 12288;

// Core 0. The Arduino loop runs on core 1, and the whole point is that the two
// no longer wait on each other. Core 0 also hosts the WiFi driver, which is
// where this work belongs anyway.
constexpr BaseType_t TASK_CORE = 0;

// Tokens are 36 characters today; this leaves room without being a buffer to
// think about.
constexpr size_t TOKEN_CAP = 64;

SemaphoreHandle_t g_lock = nullptr;  // guards everything in this block
SemaphoreHandle_t g_wake = nullptr;  // signals the task that a request is up

char        g_token[TOKEN_CAP];
uint32_t    g_stopCode = 0;
bool        g_busy = false;
bool        g_haveResult = false;
FetchResult g_result{};
Progress    g_progress{};
uint32_t    g_startedMs = 0;
uint32_t    g_phaseStartedMs = 0;  // when the current phase began
TaskHandle_t g_task = nullptr;

// Scoped mutex. Every access below goes through one of these, so there is no
// path that reads a half-written result.
struct Lock {
  Lock() { xSemaphoreTake(g_lock, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(g_lock); }
};

// Runs on the net task, called from inside siriFetch.
void onProgress(SiriPhase phase, uint32_t done, uint32_t total) {
  Lock lock;
  const uint32_t now = millis();

  // Close out the previous phase before switching, so its duration stops where
  // it actually ended rather than tracking the whole fetch.
  if (phase != g_progress.phase) {
    g_progress.phaseMs[g_progress.phase] = now - g_phaseStartedMs;
    g_phaseStartedMs = now;
    g_progress.phase = phase;
  }
  g_progress.phaseMs[phase] = now - g_phaseStartedMs;
  g_progress.done = done;
  g_progress.total = total;
  g_progress.elapsedMs = now - g_startedMs;
  g_progress.busy = true;
}

void taskMain(void*) {
  for (;;) {
    // Sleep until start() posts a request. No polling, no spinning.
    xSemaphoreTake(g_wake, portMAX_DELAY);

    char token[TOKEN_CAP];
    uint32_t stopCode;
    {
      Lock lock;
      memcpy(token, g_token, sizeof(token));
      stopCode = g_stopCode;
    }

    // The long blocking call, now off the UI core.
    const FetchResult fr = siriFetch(token, stopCode, onProgress);

    {
      Lock lock;
      g_result = fr;  // by value; see the note in net_task.h
      g_haveResult = true;
      g_busy = false;
      g_progress.busy = false;
      g_progress.elapsedMs = millis() - g_startedMs;
    }
  }
}

}  // namespace

void begin() {
  if (g_task) return;  // idempotent: a second call must not create a second task

  g_lock = xSemaphoreCreateMutex();
  g_wake = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(taskMain, "siri", TASK_STACK, nullptr,
                          // Below the Arduino loop's priority (1 is the default
                          // for loopTask; this sits at the same level and yields
                          // whenever it blocks on the network, which is most of
                          // its life).
                          1, &g_task, TASK_CORE);
}

bool start(const char* token, uint32_t stopCode) {
  if (!g_task || !token) return false;

  {
    Lock lock;
    if (g_busy) return false;  // one in flight at a time — see net_task.h
    strncpy(g_token, token, TOKEN_CAP - 1);
    g_token[TOKEN_CAP - 1] = '\0';
    g_stopCode = stopCode;
    g_busy = true;
    g_haveResult = false;
    g_startedMs = millis();
    g_phaseStartedMs = g_startedMs;
    g_progress = Progress{};
    g_progress.phase = SIRI_PHASE_CONNECT;
    g_progress.busy = true;
  }

  xSemaphoreGive(g_wake);
  return true;
}

bool busy() {
  if (!g_task) return false;
  Lock lock;
  return g_busy;
}

Progress progress() {
  if (!g_task) return Progress{};
  Lock lock;
  Progress copy = g_progress;
  // Keep the running phase moving between callbacks. CONNECT reports once and
  // then blocks for seconds; without this the screen would show a frozen
  // number for the entire wait, which is the fault this task exists to fix.
  // Only the current phase advances — finished ones keep their final duration.
  if (copy.busy) {
    const uint32_t now = millis();
    copy.elapsedMs = now - g_startedMs;
    copy.phaseMs[copy.phase] = now - g_phaseStartedMs;
  }
  return copy;
}

bool take(FetchResult* out) {
  if (!g_task || !out) return false;
  Lock lock;
  if (!g_haveResult) return false;
  *out = g_result;
  g_haveResult = false;
  return true;
}

uint32_t stackHeadroom() {
  if (!g_task) return 0;
  // Reported in words by FreeRTOS; bytes are what the stack size is expressed
  // in, so convert rather than make the reader do it.
  return (uint32_t)uxTaskGetStackHighWaterMark(g_task) * sizeof(StackType_t);
}

}  // namespace net_task
#endif  // ARDUINO
