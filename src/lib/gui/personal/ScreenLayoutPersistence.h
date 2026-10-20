/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "gui/config/ServerConfig.h"
#include <optional>

namespace deskflow::gui::personal {

// Keep the settings grid and the core's generated configuration together until
// the running server confirms that it has loaded the change.
class ScreenLayoutPersistence
{
public:
  explicit ScreenLayoutPersistence(ServerConfig &config) : m_config(config) {}
  bool isPending() const { return m_previous.has_value(); }
  bool prepare(layout::Direction expected, layout::Direction desired, QString *error);
  bool prepareMove(const layout::Move &move, QString *error);
  bool finish(bool applied, QString *error = nullptr);

private:
  bool saveCandidate(ServerConfig candidate, QString *error);
  struct Previous
  {
    ServerConfig config;
    QString path;
    QByteArray contents;
    bool existed;
  };
  ServerConfig &m_config;
  std::optional<Previous> m_previous;
};

} // namespace deskflow::gui::personal
