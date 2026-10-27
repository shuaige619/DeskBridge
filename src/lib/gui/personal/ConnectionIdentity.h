/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include <QVariantMap>

namespace deskflow::gui::personal {
// Expand a local name change into one atomic preference transaction. This only
// reads the saved identity; callers own persistence and rollback of every key.
QVariantMap expandedConnectionIdentityChanges(const QVariantMap &changes);
} // namespace deskflow::gui::personal
