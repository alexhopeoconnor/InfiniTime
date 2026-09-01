#include "displayapp/screens/HeartRate.h"
#include <lvgl/lvgl.h>
#include <components/heartrate/HeartRateController.h>

#include "displayapp/DisplayApp.h"
#include "displayapp/HeartRateReading.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  const char* ToString(Pinetime::Applications::HeartRateAcquisitionStatus status,
                       Pinetime::Applications::HeartRateMeasurementMode measurementMode) {
    const bool isBackgroundSample = measurementMode == Pinetime::Applications::HeartRateMeasurementMode::Background;
    switch (status) {
      case Pinetime::Applications::HeartRateAcquisitionStatus::Acquiring:
        return isBackgroundSample ? "Auto sample:\nacquiring signal" : "Acquiring signal...\nhold still";
      case Pinetime::Applications::HeartRateAcquisitionStatus::NoTouch:
        return "No skin contact\non sensor";
      case Pinetime::Applications::HeartRateAcquisitionStatus::SignalUnstable:
        return "Signal unstable\nhold still / tighten strap";
      case Pinetime::Applications::HeartRateAcquisitionStatus::AmbientLight:
        return "Too much light\ncover the sensor";
      case Pinetime::Applications::HeartRateAcquisitionStatus::SensorError:
        return "Sensor communication\nfailed";
      case Pinetime::Applications::HeartRateAcquisitionStatus::Running:
        return isBackgroundSample ? "Auto sample active" : "Measuring...";
      case Pinetime::Applications::HeartRateAcquisitionStatus::Stopped:
        return "Stopped";
    }
    return "";
  }

  void btnStartStopEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HeartRate*>(obj->user_data);
    screen->OnStartStopEvent(event);
  }
}

HeartRate::HeartRate(Controllers::HeartRateController& heartRateController, System::SystemTask& systemTask)
  : heartRateController {heartRateController}, wakeLock(systemTask) {
  label_hr = lv_label_create(lv_scr_act(), nullptr);

  lv_obj_set_style_local_text_font(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);

  lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);

  lv_label_set_text_static(label_hr, "---");
  lv_obj_align(label_hr, nullptr, LV_ALIGN_CENTER, 0, -40);

  label_bpm = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(label_bpm, "Heart rate BPM");
  lv_obj_align(label_bpm, label_hr, LV_ALIGN_OUT_TOP_MID, 0, -20);

  label_status = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_status, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
  lv_label_set_text_static(label_status, "Stopped");

  lv_obj_align(label_status, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  btn_startStop = lv_btn_create(lv_scr_act(), nullptr);
  btn_startStop->user_data = this;
  lv_obj_set_height(btn_startStop, 50);
  lv_obj_set_event_cb(btn_startStop, btnStartStopEventHandler);
  lv_obj_align(btn_startStop, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

  label_startStop = lv_label_create(btn_startStop, nullptr);
  UpdateMeasurementUi();

  taskRefresh = lv_task_create(RefreshTaskCallback, 100, LV_TASK_PRIO_MID, this);
}

HeartRate::~HeartRate() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HeartRate::Refresh() {

  const auto acquisitionStatus = Pinetime::Applications::GetHeartRateAcquisitionStatus(heartRateController);
  const auto measurementMode = Pinetime::Applications::GetHeartRateMeasurementMode(heartRateController);
  const auto now = xTaskGetTickCount();
  const auto readingStatus = Pinetime::Applications::GetHeartRateReadingStatus(heartRateController, now);

  if (readingStatus == Pinetime::Applications::HeartRateReadingStatus::Unavailable) {
    lv_label_set_text_static(label_hr, "---");
  } else {
    lv_label_set_text_fmt(label_hr, "%03d", heartRateController.HeartRate());
  }

  if (readingStatus == Pinetime::Applications::HeartRateReadingStatus::Stale) {
    lv_label_set_text_fmt(label_status,
                          "Signal lost\nlast good reading %lus ago",
                          Pinetime::Applications::LastHeartRateAgeSeconds(heartRateController, now));
  } else {
    lv_label_set_text_static(label_status, ToString(acquisitionStatus, measurementMode));
  }
  UpdateMeasurementUi();
  lv_obj_align(label_status, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
}

void HeartRate::OnStartStopEvent(lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    if (Pinetime::Applications::GetHeartRateMeasurementMode(heartRateController) !=
        Pinetime::Applications::HeartRateMeasurementMode::Foreground) {
      heartRateController.Enable();
    } else {
      heartRateController.Disable();
    }
    UpdateMeasurementUi();
  }
}

void HeartRate::UpdateMeasurementUi() {
  const bool isForegroundMeasurement = Pinetime::Applications::GetHeartRateMeasurementMode(heartRateController) ==
                                       Pinetime::Applications::HeartRateMeasurementMode::Foreground;
  UpdateStartStopButton(isForegroundMeasurement);
  if (isForegroundMeasurement) {
    wakeLock.Lock();
    lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  } else {
    wakeLock.Release();
    lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  }
}

void HeartRate::UpdateStartStopButton(bool isForegroundMeasurement) {
  if (isForegroundMeasurement) {
    lv_label_set_text_static(label_startStop, "Stop");
  } else {
    lv_label_set_text_static(label_startStop, "Measure now");
  }
}
