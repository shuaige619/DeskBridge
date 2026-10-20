/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once
#include <QCheckBox>

namespace deskflow::gui::personal {

// QCheckBox retains native checked-state accessibility and keyboard behavior.
class ToggleSwitch : public QCheckBox
{
public:
  explicit ToggleSwitch(QWidget *parent = nullptr);
  explicit ToggleSwitch(const QString &text, QWidget *parent = nullptr);
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override { return sizeHint(); }
protected:
  void paintEvent(QPaintEvent *event) override;
  bool hitButton(const QPoint &position) const override;
};

} // namespace deskflow::gui::personal
