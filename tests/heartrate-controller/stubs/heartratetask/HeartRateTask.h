#pragma once

namespace Pinetime::Applications {
  class HeartRateTask {
  public:
    enum class Messages { Enable, Disable, BackgroundSettingsChanged };
    void PushMessage(Messages) {
    }
  };
}
