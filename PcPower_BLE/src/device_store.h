// Known devices <-> NVS (namespace "devs").
#pragma once
#include "core/device_list.h"

namespace DeviceStore {
void load(core::DeviceList& list);
void save(const core::DeviceList& list);
}  // namespace DeviceStore
