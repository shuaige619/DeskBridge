/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include <QVariantMap>

namespace deskflow::gui::personal {

// Changes are a draft: validation and reconnect classification never write settings.
bool validatePreferenceChanges(const QVariantMap &changes, QString *error = nullptr);
bool preferencesRequireReconnect(const QVariantMap &changes);
bool commitPreferenceChanges(const QVariantMap &changes, QString *error = nullptr);

} // namespace deskflow::gui::personal
