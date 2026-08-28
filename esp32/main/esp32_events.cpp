// Where the platform's event sources will be drained: a periodic timer,
// keys, pin edges. Nothing produces them yet on this board, so the poll
// the VM calls between statements has nothing to hand over.

#include "../../src/vm.h"

void esp32_event_poll(VM&) {}
