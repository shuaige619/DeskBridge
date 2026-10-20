/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "common/ShortcutRules.h"
#include "deskflow/OptionTypes.h"

namespace deskflow::shortcuts {

// Both fields must occur exactly once, in the same DSOP message. Missing,
// duplicate, malformed or future-version metadata is deliberately unknown.
inline DeviceOS sourceOSFromOptions(const OptionsList &options)
{
  if (options.size() % 2)
    return DeviceOS::Unknown;
  bool versionSeen = false;
  bool osSeen = false;
  uint32_t version = 0;
  uint32_t os = 0;
  for (size_t i = 0; i < options.size(); i += 2) {
    if (options[i] == kOptionDeskBridgeVersion) {
      if (versionSeen)
        return DeviceOS::Unknown;
      versionSeen = true;
      version = options[i + 1];
    } else if (options[i] == kOptionDeskBridgeSourceOS) {
      if (osSeen)
        return DeviceOS::Unknown;
      osSeen = true;
      os = options[i + 1];
    }
  }
  if (!versionSeen || version != 1 || !osSeen || os > static_cast<uint32_t>(DeviceOS::Linux))
    return DeviceOS::Unknown;
  return static_cast<DeviceOS>(os);
}

} // namespace deskflow::shortcuts
