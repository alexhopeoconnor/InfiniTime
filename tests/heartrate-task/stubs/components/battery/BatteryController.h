#pragma once
namespace Pinetime::Controllers {
  class Battery {
  public:
    bool powered = false;
    bool IsPowerPresent() const { return powered; }
  };
}
