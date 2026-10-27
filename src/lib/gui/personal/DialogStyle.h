/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "PersonalStyle.h"
#include <QDialog>
#include <QEvent>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace deskflow::gui::personal {
class DialogAppearance final : public QObject
{
public:
  explicit DialogAppearance(QDialog *dialog) : QObject(dialog), m_dialog(dialog)
  {
    dialog->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override
  {
    if (watched == m_dialog && !m_updating &&
        (event->type() == QEvent::Show || event->type() == QEvent::ApplicationPaletteChange ||
         event->type() == QEvent::PaletteChange)) {
      m_updating = true;
      applyPersonalStyle(m_dialog);
      m_updating = false;
    }
    return QObject::eventFilter(watched, event);
  }

private:
  QDialog *m_dialog;
  bool m_updating = false;
};

inline void prepareDialog(QDialog *dialog, int width = 620, int height = 480)
{
  dialog->setProperty("personalDialog", true);
  dialog->setAttribute(Qt::WA_StyledBackground);
  dialog->setWindowModality(Qt::WindowModal);
  dialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  dialog->resize(width, height);
  applyPersonalStyle(dialog);
  new DialogAppearance(dialog);
}

inline QWidget *dialogHeading(const QString &title, const QString &description, QWidget *parent)
{
  auto *heading = new QWidget(parent);
  auto *layout = new QVBoxLayout(heading);
  layout->setContentsMargins(24, 22, 24, 18);
  layout->setSpacing(6);
  auto *name = new QLabel(title, heading);
  name->setTextFormat(Qt::PlainText);
  name->setProperty("pageTitle", true);
  name->setWordWrap(true);
  layout->addWidget(name);
  if (!description.isEmpty()) {
    auto *detail = new QLabel(description, heading);
    detail->setTextFormat(Qt::PlainText);
    detail->setProperty("secondaryText", true);
    detail->setWordWrap(true);
    layout->addWidget(detail);
  }
  return heading;
}

inline QScrollArea *dialogScroll(QWidget *content, QWidget *parent)
{
  content->setProperty("personalPageBody", true);
  auto *scroll = new QScrollArea(parent);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(content);
  return scroll;
}
} // namespace deskflow::gui::personal
