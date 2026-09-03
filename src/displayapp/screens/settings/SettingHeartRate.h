#pragma once

#include <cstdint>
#include <lvgl/lvgl.h>
#include <optional>
#include <array>

#include "components/settings/Settings.h"
#include "displayapp/screens/Screen.h"

namespace Pinetime {

  namespace Controllers {
    class HeartRateController;
  }

  namespace Applications {
    namespace Screens {
      class SettingHeartRate : public Screen {
      public:
        SettingHeartRate(Pinetime::Controllers::Settings& settings, Pinetime::Controllers::HeartRateController& heartRateController);
        ~SettingHeartRate() override;

        void UpdateSelected(lv_obj_t* object, lv_event_t event);

      private:
        struct Option {
          std::optional<uint16_t> intervalInSeconds;
          const char* name;
        };

        Pinetime::Controllers::Settings& settingsController;
        Pinetime::Controllers::HeartRateController& heartRateController;

        static constexpr std::array<Option, 7> options = {{
          {.intervalInSeconds = std::nullopt, .name = "Off (hide)"},
          {.intervalInSeconds = 0, .name = "Live"},
          {.intervalInSeconds = 30, .name = "30 sec"},
          {.intervalInSeconds = 60, .name = "1 min"},
          {.intervalInSeconds = 5 * 60, .name = "5 min"},
          {.intervalInSeconds = 10 * 60, .name = "10 min"},
          {.intervalInSeconds = 30 * 60, .name = "30 min"},
        }};

        lv_obj_t* cbOption[options.size()];
      };
    }
  }
}
