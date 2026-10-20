/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ToggleSwitch.h"
#include "PersonalStyle.h"
#include <QPainter>
#include <QPalette>

namespace deskflow::gui::personal {

ToggleSwitch::ToggleSwitch(QWidget *parent) : ToggleSwitch(QString(), parent) {}

ToggleSwitch::ToggleSwitch(const QString &text, QWidget *parent) : QCheckBox(text, parent)
{
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
  const int labelWidth = text().isEmpty() ? 0 : fontMetrics().horizontalAdvance(text()) + 12;
  return {42 + labelWidth, qMax(26, fontMetrics().height() + 4)};
}

bool ToggleSwitch::hitButton(const QPoint &position) const
{
  return rect().contains(position);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF track(width() - 40, (height() - 23) / 2.0, 38, 23);
  const auto colors = palette();
  const bool dark = colors.color(QPalette::Window).lightness() < 128;
  const QColor inactive = dark ? QColor(91, 94, 100) : QColor(188, 191, 197);
  // Dim the control independently of its label. The disabled palette already
  // dims text on macOS; applying opacity to both makes the label unreadable.
  painter.setOpacity(isEnabled() ? 1.0 : 0.45);
  painter.setPen(Qt::NoPen);
  painter.setBrush(isChecked() ? personalAccentColor() : inactive);
  painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);

  const qreal diameter = 19;
  const qreal x = isChecked() ? track.right() - diameter - 2 : track.left() + 2;
  painter.setBrush(QColor(0, 0, 0, 25));
  painter.drawEllipse(QRectF(x, track.top() + 3, diameter, diameter));
  painter.setBrush(Qt::white);
  painter.drawEllipse(QRectF(x, track.top() + 2, diameter, diameter));
  painter.setOpacity(1.0);

  if (!text().isEmpty()) {
    painter.setPen(isEnabled() ? colors.color(QPalette::Active, QPalette::WindowText)
                              : QColor(dark ? "#92929c" : "#85858f"));
    painter.drawText(QRect(0, 0, qMax(0, width() - 52), height()), Qt::AlignVCenter | Qt::AlignLeft, text());
  }
  if (hasFocus()) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(personalAccentColor(), 1.5, Qt::DotLine));
    painter.drawRoundedRect(track.adjusted(-1, -1, 1, 1), 13, 13);
  }
}

} // namespace deskflow::gui::personal
