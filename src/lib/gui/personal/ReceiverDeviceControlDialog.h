/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "common/ScreenLayout.h"
#include <QDialog>

namespace deskflow::gui::personal {

// Receiver-side overview. Source settings are intentionally not loaded from the
// receiver's dormant ServerConfig; only the source's actual layout is supplied.
class ReceiverDeviceControlDialog : public QDialog
{
  Q_OBJECT
public:
  enum class Destination { None, Layout, Shortcuts, Connection };

  explicit ReceiverDeviceControlDialog(
      const layout::Snapshot &snapshot, const QString &sourceAddress, const QString &localName, bool connected,
      QWidget *parent = nullptr
  );

  Destination destination() const;

private:
  Destination m_destination = Destination::None;
};

} // namespace deskflow::gui::personal
