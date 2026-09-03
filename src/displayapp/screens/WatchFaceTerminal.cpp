#include <lvgl/lvgl.h>
#include "displayapp/screens/WatchFaceTerminal.h"
#include "displayapp/screens/BatteryIcon.h"
#include "components/battery/BatteryController.h"
#include "components/ble/BleController.h"
#include "components/ble/NotificationManager.h"
#include "components/heartrate/HeartRateController.h"
#include "components/motion/MotionController.h"
#include "components/settings/Settings.h"
#include "displayapp/InfiniTimeTheme.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

WatchFaceTerminal::WatchFaceTerminal(Controllers::DateTime& dateTimeController,
                                     const Controllers::Battery& batteryController,
                                     const Controllers::Ble& bleController,
                                     Controllers::NotificationManager& notificationManager,
                                     Controllers::Settings& settingsController,
                                     Controllers::HeartRateController& heartRateController,
                                     Controllers::MotionController& motionController)
  : currentDateTime {{}},
    dateTimeController {dateTimeController},
    batteryController {batteryController},
    bleController {bleController},
    notificationManager {notificationManager},
    settingsController {settingsController},
    heartRateController {heartRateController},
    motionController {motionController} {

  container = lv_cont_create(lv_scr_act(), nullptr);
  lv_cont_set_layout(container, LV_LAYOUT_COLUMN_LEFT);
  lv_cont_set_fit(container, LV_FIT_TIGHT);
  lv_obj_set_style_local_pad_inner(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, -3);
  lv_obj_set_style_local_bg_opa(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);

  notificationIcon = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_set_style_local_text_color(notificationIcon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);

  labelPrompt1 = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(labelPrompt1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_obj_set_style_local_text_color(labelPrompt1, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  lv_label_set_text_static(labelPrompt1, "elixir@time $");

  labelTime = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(labelTime, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(labelTime, true);

  labelDate = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(labelDate, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(labelDate, true);

  batteryValue = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(batteryValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(batteryValue, true);

  stepValue = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(stepValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(stepValue, true);
  lv_obj_set_style_local_text_color(stepValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);

  heartbeatValue = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(heartbeatValue, true);

  connectState = lv_label_create(container, nullptr);
  lv_obj_set_style_local_text_font(connectState, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_recolor(connectState, true);

  lv_obj_align(container, nullptr, LV_ALIGN_IN_TOP_LEFT, 0, 7);

  taskRefresh = lv_task_create(RefreshTaskCallback, LV_DISP_DEF_REFR_PERIOD, LV_TASK_PRIO_MID, this);
  Refresh();
}

WatchFaceTerminal::~WatchFaceTerminal() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void WatchFaceTerminal::Refresh() {
  notificationState = notificationManager.AreNewNotificationsAvailable();
  if (notificationState.IsUpdated()) {
    if (notificationState.Get()) {
      lv_obj_set_hidden(notificationIcon, false);
      lv_label_set_text_fmt(notificationIcon, "%s new", Symbols::bell);
    } else {
      lv_obj_set_hidden(notificationIcon, true);
    }
  }

  currentDateTime = std::chrono::time_point_cast<std::chrono::seconds>(dateTimeController.CurrentDateTime());
  if (currentDateTime.IsUpdated()) {
    uint8_t hour = dateTimeController.Hours();
    uint8_t minute = dateTimeController.Minutes();
    uint8_t second = dateTimeController.Seconds();

    if (settingsController.GetClockType() == Controllers::Settings::ClockType::H12) {
      char ampmChar[3] = "AM";
      if (hour == 0) {
        hour = 12;
      } else if (hour == 12) {
        ampmChar[0] = 'P';
      } else if (hour > 12) {
        hour = hour - 12;
        ampmChar[0] = 'P';
      }
      lv_label_set_text_fmt(labelTime, "#ffffff %s# #11cc55 %02d:%02d:%02d %s#", Symbols::clock, hour, minute, second, ampmChar);
    } else {
      lv_label_set_text_fmt(labelTime, "#ffffff %s# #11cc55 %02d:%02d:%02d#", Symbols::clock, hour, minute, second);
    }

    currentDate = std::chrono::time_point_cast<std::chrono::days>(currentDateTime.Get());
    if (currentDate.IsUpdated()) {
      uint16_t year = dateTimeController.Year();
      Controllers::DateTime::Months month = dateTimeController.Month();
      uint8_t day = dateTimeController.Day();
      lv_label_set_text_fmt(labelDate, "#ffffff %s# #007fff %04d-%02d-%02d#", Symbols::calendar, year, month, day);
    }
  }

  const bool isPowerPresent = batteryController.IsPowerPresent();
  powerPresent = isPowerPresent;
  const bool powerPresentChanged = powerPresent.IsUpdated();
  batteryPercentRemaining = batteryController.PercentRemaining();
  if (batteryPercentRemaining.IsUpdated() || powerPresentChanged) {
    lv_obj_set_style_local_text_color(batteryValue,
                                      LV_LABEL_PART_MAIN,
                                      LV_STATE_DEFAULT,
                                      BatteryIcon::ColorFromPercentage(batteryPercentRemaining.Get()));
    if (batteryController.IsCharging()) {
      lv_label_set_text_fmt(batteryValue,
                            "#ffffff %s# %d%% %s",
                            Symbols::batteryHalf,
                            batteryPercentRemaining.Get(),
                            Symbols::plug);
    } else {
      lv_label_set_text_fmt(batteryValue, "#ffffff %s# %d%%", Symbols::batteryHalf, batteryPercentRemaining.Get());
    }
  }

  stepCount = motionController.NbSteps();
  if (stepCount.IsUpdated()) {
    lv_label_set_text_fmt(stepValue, "#ffffff %s# %lu", Symbols::shoe, stepCount.Get());
  }

  heartRateEnabled = settingsController.GetHeartRateBackgroundMeasurementInterval().has_value();
  const bool heartRateEnabledChanged = heartRateEnabled.IsUpdated();
  if (heartRateEnabledChanged) {
    lv_obj_set_hidden(heartbeatValue, !heartRateEnabled.Get());
  }
  if (heartRateEnabled.Get()) {
    if (isPowerPresent) {
      if (heartRateEnabledChanged || powerPresentChanged) {
        lv_label_set_text_fmt(heartbeatValue, "#ffffff %s# dock", Symbols::heartBeat);
        lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
      }
    } else {
      heartbeat = heartRateController.HeartRate();
      heartbeatRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
      const auto now = xTaskGetTickCount();
      heartRateAgeSeconds = LastHeartRateAgeSeconds(heartRateController, now);
      const auto heartRateStatus = GetHeartRateReadingStatus(heartRateController, now);
      const auto acquisitionStatus = GetHeartRateAcquisitionStatus(heartRateController);
      const bool activeAcquisitionIsDegraded = acquisitionStatus != HeartRateAcquisitionStatus::Stopped &&
                                               acquisitionStatus != HeartRateAcquisitionStatus::Running;
      const auto displayStaleHeartRate = [&] {
        const auto age = heartRateAgeSeconds.Get();
        lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::orange);
        lv_label_set_text_fmt(heartbeatValue,
                              "#ffffff %s# ~%d %02lu:%02lu",
                              Symbols::heartBeat,
                              heartbeat.Get(),
                              age / 60,
                              age % 60);
      };
      if (heartRateEnabledChanged || powerPresentChanged || heartbeat.IsUpdated() || heartbeatRunning.IsUpdated() ||
          heartRateAgeSeconds.IsUpdated() || heartRateStatus != lastHeartRateStatus) {
        lastHeartRateStatus = heartRateStatus;
        switch (heartRateStatus) {
          case HeartRateReadingStatus::Fresh:
            // A previously good value is still useful during a new sample, but
            // do not present it as a fresh live reading while acquisition says
            // the optical signal is failing.
            if (activeAcquisitionIsDegraded) {
              displayStaleHeartRate();
            } else {
              lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::deepOrange);
              lv_label_set_text_fmt(heartbeatValue, "#ffffff %s# %d", Symbols::heartBeat, heartbeat.Get());
            }
            break;
          case HeartRateReadingStatus::Stale:
            displayStaleHeartRate();
            break;
          case HeartRateReadingStatus::Unavailable:
            lv_label_set_text_fmt(heartbeatValue,
                                  "#ffffff %s# %s",
                                  Symbols::heartBeat,
                                  heartbeatRunning.Get() ? "acq" : "---");
            lv_obj_set_style_local_text_color(heartbeatValue, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
            break;
        }
      }
    }
  }

  bleState = bleController.IsConnected();
  bleRadioEnabled = bleController.IsRadioEnabled();
  if (bleState.IsUpdated() || bleRadioEnabled.IsUpdated()) {
    if (!bleRadioEnabled.Get()) {
      lv_label_set_text_fmt(connectState, "#ffffff %s# off", Symbols::bluetooth);
      lv_obj_set_style_local_text_color(connectState, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
    } else {
      if (bleState.Get()) {
        lv_label_set_text_fmt(connectState, "#ffffff %s# on", Symbols::bluetooth);
        lv_obj_set_style_local_text_color(connectState, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::blue);
      } else {
        lv_label_set_text_fmt(connectState, "#ffffff %s# --", Symbols::bluetooth);
        lv_obj_set_style_local_text_color(connectState, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::gray);
      }
    }
  }
}
