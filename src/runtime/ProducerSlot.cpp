// Repository: Retrovue-playout
// Component: Producer Slot
// Purpose: Implementation of ProducerSlot.
// Copyright (c) 2025 RetroVue

#include "retrovue/runtime/ProducerSlot.h"
#include "retrovue/producers/IProducer.h"

namespace retrovue {

void ProducerSlot::reset() {
  if (producer) {
    if (producer->isRunning()) {
      producer->stop();
    }
    producer.reset();
  }
  loaded = false;
  asset_id.clear();
  file_path.clear();
}

} // namespace retrovue

