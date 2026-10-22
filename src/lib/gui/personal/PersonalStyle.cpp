/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "PersonalStyle.h"

#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QToolButton>
#include <QWidget>

namespace deskflow::gui::personal {

QString displayOperatingSystem(shortcuts::DeviceOS os)
{
  switch (os) {
  case shortcuts::DeviceOS::MacOS:
    return QStringLiteral("macOS");
  case shortcuts::DeviceOS::Windows:
    return QStringLiteral("Windows");
  case shortcuts::DeviceOS::Linux:
    return QStringLiteral("Linux");
  default:
    return QStringLiteral("未知系统");
  }
}

QColor personalAccentColor()
{
  const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
  return QColor(dark ? QStringLiteral("#0a84ff") : QStringLiteral("#0067d9"));
}

void applyPersonalStyle(QWidget *widget)
{
  const auto palette = QApplication::palette();
  const bool dark = palette.color(QPalette::Window).lightness() < 128;
  const QString surface = dark ? QStringLiteral("#292a2f") : QStringLiteral("#ffffff");
  const QString background = dark ? QStringLiteral("#202125") : QStringLiteral("#f6f6f8");
  const QString text = palette.color(QPalette::WindowText).name();
  // macOS supplies translucent label colors. QColor::name() drops their alpha,
  // turning secondary labels black; use explicit opaque tones on our surfaces.
  const QString secondary = dark ? QStringLiteral("#aaaab4") : QStringLiteral("#686873");
  const QString accent = personalAccentColor().name();
  const QString separator = dark ? QStringLiteral("#3d3e45") : QStringLiteral("#e1e1e7");
  const QString sidebar = dark ? QStringLiteral("#26262b") : QStringLiteral("#ececf1");
  const QString danger = dark ? QStringLiteral("#ffb4a4") : QStringLiteral("#b34024");
  QString style =
      QStringLiteral(
          "QWidget#personalWindow, QWidget[personalPageBody=\"true\"] { background: %1; color: %2; }"
          "QScrollArea { background: transparent; border: none; }"
          "QFrame#personalSidebar { background: %3; border-right: 1px solid %4; }"
          "QToolBar#personalToolbar { background: %1; border: 0; border-bottom: 1px solid %4; spacing: 8px; padding: "
          "18px 28px; }"
          "QWidget[personalCard=\"true\"] { background: %5; border: 1px solid %4; border-radius: 12px; }"
          "QFrame[personalSeparator=\"true\"] { background: %4; border: none; min-height: 1px; max-height: 1px; }"
          "QWidget[personalFooter=\"true\"] { background: %1; border-top: 1px solid %4; }"
          "QLabel[secondaryText=\"true\"] { color: %6; font-size: 12px; background: transparent; border: none; }"
          "QLabel[pageTitle=\"true\"] { font-size: 21px; font-weight: 600; background: transparent; }"
          "QLabel[pageSubtitle=\"true\"] { color: %6; font-size: 12px; background: transparent; }"
          "QLabel[sectionTitle=\"true\"] { color: %2; font-size: 13px; font-weight: 600; background: transparent; "
          "border: none; }"
          "QLabel[statusBadge=\"true\"] { color: %8; background: transparent; border: none; padding: 3px 6px; }"
          "QLabel[dangerText=\"true\"] { color: %7; background: transparent; border: none; }"
          "QLabel[keycap=\"true\"] { border: 1px solid %4; background: %1; border-radius: 5px; padding: 3px 7px; }"
          "QPushButton[keycap=\"true\"] { border: 1px solid %4; background: %5; border-radius: 6px; padding: 6px 12px; "
          "min-height: 26px; }"
          "QPushButton[keycap=\"true\"][recording=\"true\"] { border: 2px solid %8; }"
          "QWidget[applicationChip=\"true\"] { background: %3; border: 1px solid %4; border-radius: 9px; }"
          "QWidget[applicationChip=\"true\"] QLabel { background: transparent; border: none; }"
          "QPushButton { background: %5; color: %2; border: 1px solid %4; border-radius: 7px; min-height: 20px; "
          "padding: 5px 12px; }"
          "QPushButton:hover:enabled { background: %1; }"
          "QPushButton:pressed:enabled { background: %3; }"
          "QPushButton:focus { border-color: %8; }"
          "QPushButton:checked { color: %8; border-color: %8; }"
          "QPushButton:disabled { background: %1; color: %6; border-color: %4; }"
          "QPushButton[dangerButton=\"true\"] { color: %7; }"
          "QPushButton[dangerButton=\"true\"]:disabled { color: %6; }"
          "QPushButton[primaryButton=\"true\"] { background: %8; color: white; border: 1px solid %8; border-radius: "
          "7px; padding: 5px 14px; }"
          "QPushButton[primaryButton=\"true\"]:hover:enabled { background: %8; }"
          "QPushButton[primaryButton=\"true\"]:focus:enabled { border: 2px solid %2; padding: 4px 13px; }"
          "QPushButton[primaryButton=\"true\"]:disabled { background: %3; color: %6; border-color: %4; }"
          "QPushButton[personalNav=\"true\"] { border: 1px solid transparent; border-radius: 8px; background: "
          "transparent; text-align: left; padding: 9px 12px; font-size: 13px; }"
          "QPushButton[personalNav=\"true\"]:checked { background: %8; color: white; }"
          "QPushButton[personalNav=\"true\"]:hover:!checked { background: %4; }"
          "QPushButton[personalNav=\"true\"]:focus { border-color: %8; }"
          "QPushButton[personalNav=\"true\"]:focus:checked { border-color: white; }"
          "QPushButton[personalRow=\"true\"] { background: %5; border: 1px solid %4; border-radius: 9px; min-height: "
          "76px; padding: 0; text-align: left; }"
          "QPushButton[personalRow=\"true\"]:hover { border-color: %8; }"
          "QPushButton[personalRow=\"true\"]:focus { border-color: %8; }"
          "QToolButton[personalDisclosure=\"true\"] { background: transparent; color: %2; border: 1px solid "
          "transparent; border-radius: 6px; padding: 6px 4px; min-height: 20px; font-size: 13px; }"
          "QToolButton[personalDisclosure=\"true\"]:hover { background: %1; }"
          "QToolButton[personalDisclosure=\"true\"]:focus { border-color: %8; }"
      )
          .arg(background, text, sidebar, separator, surface, secondary, danger, accent);

  // These rules stay inside the personal window and its sheets. Keep the
  // native line-edit clear button and reserve a separate combo arrow area.
  style +=
      QStringLiteral(
          "QLineEdit, QComboBox { background: %1; color: %2; border: 1px solid %3; border-radius: 7px;"
          " min-height: 20px; padding: 6px 10px; selection-background-color: %4; selection-color: white; }"
          "QLineEdit:hover:!focus:enabled, QComboBox:hover:!focus:enabled { border-color: %5; }"
          "QLineEdit:focus, QComboBox:focus, QComboBox:on { border-color: %4; }"
          "QLineEdit:disabled, QComboBox:disabled { background: %6; color: %7; border-color: %3; }"
          "QComboBox { padding-right: 36px; }"
          "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right;"
          " width: 30px; border: none; background: transparent; }"
          "QComboBox::down-arrow { image: url(%8); width: 12px; height: 12px; }"
          "QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3;"
          " border-radius: 7px; padding: 4px; outline: 0; selection-background-color: %4; selection-color: white; }"
          "QComboBox QAbstractItemView::item { min-height: 28px; padding: 2px 8px; }"
      )
          .arg(
              surface, text, separator, accent, dark ? QStringLiteral("#777780") : QStringLiteral("#a6a6b0"), sidebar,
              dark ? QStringLiteral("#84848e") : QStringLiteral("#9999a3"),
              QStringLiteral(":/icons/deskflow-%1/actions/16/go-down.svg").arg(dark ? "dark" : "light")
          );
  style +=
      QStringLiteral(
          "QAbstractSpinBox { background: %1; color: %2; border: 1px solid %3; border-radius: 7px;"
          " min-height: 20px; padding: 5px 24px 5px 10px; selection-background-color: %4; selection-color: white; }"
          "QAbstractSpinBox:focus { border-color: %4; }"
          "QAbstractSpinBox:disabled { background: %5; color: %6; }"
          "QAbstractSpinBox QLineEdit { background: transparent; border: none; padding: 0; min-height: 0; }"
          "QAbstractSpinBox QLineEdit:disabled { background: transparent; color: %6; border: none; }"
          "QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; "
          "width: 22px; border: none; background: transparent; }"
          "QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom "
          "right; width: 22px; border: none; background: transparent; }"
          "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url(%7); width: 10px; height: 10px; }"
          "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url(%8); width: 10px; height: 10px; }"
      )
          .arg(
              surface, text, separator, accent, background, secondary,
              QStringLiteral(":/icons/deskflow-%1/actions/16/go-up.svg").arg(dark ? "dark" : "light"),
              QStringLiteral(":/icons/deskflow-%1/actions/16/go-down.svg").arg(dark ? "dark" : "light")
          );
  style +=
      QStringLiteral(
          "QDialog[personalDialog=\"true\"] { background: %1; color: %2; }"
          "QLabel[dangerText=\"true\"] { color: %7; }"
          "QListWidget, QTableView, QTextBrowser, QPlainTextEdit { background: %5; color: %2;"
          " border: 1px solid %4; border-radius: 8px; selection-background-color: %8; selection-color: white; }"
          "QListWidget { padding: 5px; outline: 0; }"
          "QListWidget::item { padding: 7px 9px; border-radius: 5px; min-height: 20px; }"
          "QListWidget::item:hover:!selected { background: %3; }"
          "QListWidget::item:selected { background: %8; color: white; }"
          "QListWidget[dialogNavigation=\"true\"] { background: transparent; border: 0; padding: 0; }"
          "QListWidget[dialogNavigation=\"true\"]::item { padding: 5px 10px; }"
          "QTableView { gridline-color: %4; }"
          "QTextBrowser, QPlainTextEdit { padding: 10px; }"
          "QCheckBox, QRadioButton { color: %2; spacing: 8px; min-height: 25px; background: transparent; }"
          "QCheckBox:disabled, QRadioButton:disabled { color: %6; }"
          "QCheckBox::indicator, QRadioButton::indicator, QListWidget::indicator { width: 16px; height: 16px; border: 1px solid %6; "
          "background: %5; }"
          "QCheckBox::indicator, QListWidget::indicator { border-radius: 3px; }"
          "QRadioButton::indicator { border-radius: 9px; }"
          "QCheckBox::indicator:checked, QListWidget::indicator:checked { background: %8; border-color: %8; image: url(:/icons/deskbridge-check.svg); }"
          "QRadioButton::indicator:checked { background: %8; border-color: %8; image: "
          "url(:/icons/deskbridge-radio.svg); }"
          "QCheckBox::indicator:disabled, QRadioButton::indicator:disabled, QListWidget::indicator:disabled { border-color: %4; background: %1; }"
          "QCheckBox::indicator:checked:disabled, QRadioButton::indicator:checked:disabled, QListWidget::indicator:checked:disabled { background: %6; }"
          "QTabWidget::pane { border: 1px solid %4; border-radius: 8px; background: %5; top: -1px; }"
          "QTabBar::tab { background: %1; color: %6; border: 1px solid transparent; padding: 9px 14px; min-height: "
          "18px; }"
          "QTabBar::tab:selected { background: %5; color: %2; border: 1px solid %4; border-top-left-radius: 7px; "
          "border-top-right-radius: 7px; }"
          "QTabBar::tab:hover:!selected { color: %8; }"
          "QGroupBox { background: %5; color: %2; border: 1px solid %4; border-radius: 9px;"
          " margin-top: 12px; padding: 16px; font-weight: 600; }"
          "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 14px; padding: 0 4px; }"
      )
          .arg(background, text, sidebar, separator, surface, secondary, danger, accent);
  style +=
      QStringLiteral(
          "QToolButton[applicationRemove=\"true\"] { background: transparent; border: 1px solid transparent;"
          " border-radius: 12px; padding: 0; margin: 0; }"
          "QToolButton[applicationRemove=\"true\"]:hover:enabled { background: %1; }"
          "QToolButton[applicationRemove=\"true\"]:pressed:enabled { background: %2; }"
          "QToolButton[applicationRemove=\"true\"]:focus { border-color: %3; }"
          "QToolButton[applicationRemove=\"true\"]:disabled { background: transparent; border-color: transparent; }"
      )
          .arg(
              dark ? QStringLiteral("#593a39") : QStringLiteral("#f8e6e3"),
              dark ? QStringLiteral("#754440") : QStringLiteral("#f1cfca"), accent
          );
  widget->setStyleSheet(style);
  for (auto *button : widget->findChildren<QToolButton *>()) {
    if (button->property("applicationRemove").toBool())
      button->setIcon(personalCloseIcon());
    if (button->property("personalDisclosure").toBool())
      button->setIcon(personalDisclosureIcon(button->isChecked()));
  }
}

QIcon personalIcon(const QString &name, const QColor &color)
{
  QPixmap image(48, 48);
  image.fill(Qt::transparent);
  QPainter p(&image);
  p.setRenderHint(QPainter::Antialiasing);
  p.scale(2, 2);
  p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (name == QLatin1String("devices")) {
    p.drawRoundedRect(QRectF(3, 4, 18, 12), 1, 1);
    p.drawLine(12, 16, 12, 20);
    p.drawLine(8, 20, 16, 20);
  } else if (name == QLatin1String("keyboard")) {
    p.drawRoundedRect(QRectF(2, 6, 20, 13), 2, 2);
    for (int y = 9; y <= 12; y += 3)
      for (int x = 5; x <= 19; x += 3)
        p.drawPoint(x, y);
    p.drawLine(7, 16, 17, 16);
  } else if (name == QLatin1String("close")) {
    p.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(5, 5), QPointF(19, 19));
    p.drawLine(QPointF(19, 5), QPointF(5, 19));
  } else if (name == QLatin1String("bridge")) {
    p.drawArc(QRectF(4, 4, 16, 18), 0, 180 * 16);
    p.drawLine(2, 13, 22, 13);
    p.drawLine(5, 13, 5, 19);
    p.drawLine(19, 13, 19, 19);
  } else if (name == QLatin1String("advanced")) {
    QPainterPath shield;
    shield.moveTo(12, 3);
    shield.lineTo(20, 6);
    shield.lineTo(20, 12);
    shield.cubicTo(20, 17, 16, 20, 12, 22);
    shield.cubicTo(8, 20, 4, 17, 4, 12);
    shield.lineTo(4, 6);
    shield.closeSubpath();
    p.drawPath(shield);
    p.drawLine(QPointF(8.5, 12), QPointF(11, 14.5));
    p.drawLine(QPointF(11, 14.5), QPointF(15.5, 9.5));
  } else if (name == QLatin1String("chevron-down") || name == QLatin1String("chevron-right")) {
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath chevron;
    if (name == QLatin1String("chevron-down")) {
      chevron.moveTo(5, 9);
      chevron.lineTo(12, 16);
      chevron.lineTo(19, 9);
    } else {
      chevron.moveTo(9, 5);
      chevron.lineTo(16, 12);
      chevron.lineTo(9, 19);
    }
    p.drawPath(chevron);
  } else {
    for (int y : {6, 12, 18})
      p.drawLine(3, y, 21, y);
    p.setBrush(QApplication::palette().color(QPalette::Window));
    p.drawEllipse(QPointF(8, 6), 2, 2);
    p.drawEllipse(QPointF(16, 12), 2, 2);
    p.drawEllipse(QPointF(10, 18), 2, 2);
  }
  return QIcon(image);
}

QIcon personalDisclosureIcon(bool expanded)
{
  const bool dark = QApplication::palette().color(QPalette::Window).lightness() < 128;
  return personalIcon(
      expanded ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"), QColor(dark ? "#aaaab4" : "#686873")
  );
}

QIcon personalCloseIcon()
{
  const auto palette = QApplication::palette();
  const bool dark = palette.color(QPalette::Window).lightness() < 128;
  auto icon = personalIcon(QStringLiteral("close"), palette.color(QPalette::PlaceholderText));
  icon.addPixmap(
      personalIcon(QStringLiteral("close"), QColor(dark ? "#ffb4a4" : "#b34024")).pixmap(48, 48), QIcon::Active
  );
  icon.addPixmap(
      personalIcon(QStringLiteral("close"), palette.color(QPalette::Disabled, QPalette::Text)).pixmap(48, 48),
      QIcon::Disabled
  );
  return icon;
}
} // namespace deskflow::gui::personal
