#include "displayapp/screens/settings/SettingHeartRate.h"
#include <lvgl/lvgl.h>
#include "components/heartrate/HeartRateController.h"
#include "displayapp/screens/Styles.h"
#include "displayapp/screens/Symbols.h"

using namespace Pinetime::Applications::Screens;

namespace {
  void EventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<SettingHeartRate*>(obj->user_data);
    screen->UpdateSelected(obj, event);
  }

  template <typename Controller>
  void NotifyBackgroundSettingsChanged(Controller& controller) {
    // InfiniSim provides a lightweight heart-rate controller. It has no
    // sensor task to reschedule, while the device controller does.
    if constexpr (requires { controller.OnBackgroundSettingsChanged(); }) {
      controller.OnBackgroundSettingsChanged();
    }
  }
}

SettingHeartRate::SettingHeartRate(Pinetime::Controllers::Settings& settingsController,
                                   Pinetime::Controllers::HeartRateController& heartRateController)
  : settingsController {settingsController}, heartRateController {heartRateController} {
  lv_obj_t* container = lv_cont_create(lv_scr_act(), nullptr);

  lv_obj_set_style_local_bg_opa(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, LV_OPA_TRANSP);
  lv_obj_set_style_local_pad_all(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_pad_inner(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 5);
  lv_obj_set_style_local_border_width(container, LV_CONT_PART_MAIN, LV_STATE_DEFAULT, 0);

  lv_obj_set_pos(container, 10, 60);
  lv_obj_set_width(container, LV_HOR_RES - 20);
  lv_obj_set_height(container, LV_VER_RES - 50);
  lv_cont_set_layout(container, LV_LAYOUT_PRETTY_TOP);

  lv_obj_t* title = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(title, "Heart rate");
  lv_label_set_align(title, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(title, lv_scr_act(), LV_ALIGN_IN_TOP_MID, 10, 15);

  lv_obj_t* icon = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);
  lv_obj_set_style_local_text_font(icon, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_bold_20);
  lv_label_set_text_static(icon, Symbols::heartBeat);
  lv_label_set_align(icon, LV_LABEL_ALIGN_CENTER);
  lv_obj_align(icon, title, LV_ALIGN_OUT_LEFT_MID, -10, 0);

  std::optional<uint16_t> currentInterval = settingsController.GetHeartRateBackgroundMeasurementInterval();

  for (std::size_t i = 0; i < options.size(); i++) {
    cbOption[i] = lv_checkbox_create(container, nullptr);
    lv_checkbox_set_text(cbOption[i], options[i].name);
    cbOption[i]->user_data = this;
    lv_obj_set_event_cb(cbOption[i], EventHandler);
    SetRadioButtonStyle(cbOption[i]);

    if (options[i].intervalInSeconds == currentInterval) {
      lv_checkbox_set_checked(cbOption[i], true);
    }
  }
}

SettingHeartRate::~SettingHeartRate() {
  lv_obj_clean(lv_scr_act());
  settingsController.SaveSettings();
}

void SettingHeartRate::UpdateSelected(lv_obj_t* object, lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    for (std::size_t i = 0; i < options.size(); i++) {
      if (object == cbOption[i]) {
        lv_checkbox_set_checked(cbOption[i], true);
        settingsController.SetHeartRateBackgroundMeasurementInterval(options[i].intervalInSeconds);
        NotifyBackgroundSettingsChanged(heartRateController);
      } else {
        lv_checkbox_set_checked(cbOption[i], false);
      }
    }
  }
}
