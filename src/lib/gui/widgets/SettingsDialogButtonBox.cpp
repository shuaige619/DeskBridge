/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SettingsDialogButtonBox.h"
#include "common/Settings.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>

SettingsDialogButtonBox::SettingsDialogButtonBox(QWidget *parent)
    : QWidget{parent},
      m_buttonBox{new QDialogButtonBox(this)},
      m_lblText{new QLabel(this)},
      m_lblIcon{new QLabel(this)}
{
  m_lblIcon->setPixmap(QIcon::fromTheme(QIcon::ThemeIcon::DialogWarning).pixmap(24, 24));
  m_buttonBox->addButton(QDialogButtonBox::Save);
  m_buttonBox->addButton(QDialogButtonBox::Cancel);
  m_buttonBox->addButton(QDialogButtonBox::Reset);
  m_buttonBox->addButton(QDialogButtonBox::RestoreDefaults);
  settingsWritableChanged(Settings::isWritable());

  auto layout = new QHBoxLayout(this);
  setProperty("personalFooter", true);
  layout->setContentsMargins(24, 14, 24, 16);
  layout->setSpacing(8);
  layout->addWidget(m_lblIcon);
  layout->addWidget(m_lblText);
  layout->addStretch();
  layout->addWidget(m_buttonBox);
  setLayout(layout);

  updateText();

  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialogButtonBox::accepted);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialogButtonBox::rejected);
  connect(m_buttonBox->button(QDialogButtonBox::Reset), &QPushButton::clicked, this, &SettingsDialogButtonBox::reset);
  connect(
      m_buttonBox->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
      &SettingsDialogButtonBox::restoreDefault
  );
  connect(
      Settings::instance(), &Settings::settingsWritableChanged, this, &SettingsDialogButtonBox::settingsWritableChanged
  );
}

void SettingsDialogButtonBox::enableSave(bool enable) const
{
  m_buttonBox->button(QDialogButtonBox::Save)->setEnabled(enable);
}

void SettingsDialogButtonBox::enableReset(bool enable) const
{
  m_buttonBox->button(QDialogButtonBox::Reset)->setEnabled(enable);
}

void SettingsDialogButtonBox::enableRestoreDefaults(bool enable) const
{
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setEnabled(enable);
}

void SettingsDialogButtonBox::changeEvent(QEvent *e)
{
  QWidget::changeEvent(e);
  if (e->type() == QEvent::LanguageChange)
    updateText();
}

void SettingsDialogButtonBox::updateText()
{
  m_lblText->setText(tr("设置为只读"));
  m_lblText->setToolTip(tr("无法写入 %1").arg(Settings::settingsFile()));
  m_buttonBox->button(QDialogButtonBox::Save)->setText(tr("保存更改"));
  m_buttonBox->button(QDialogButtonBox::Save)->setProperty("primaryButton", true);
  m_buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  m_buttonBox->button(QDialogButtonBox::Reset)->setText(tr("还原更改"));
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setText(tr("恢复默认"));

  m_buttonBox->button(QDialogButtonBox::Save)->setToolTip(tr("保存本窗口的更改并关闭"));
  m_buttonBox->button(QDialogButtonBox::Cancel)->setToolTip(tr("放弃本窗口的更改并关闭"));
  m_buttonBox->button(QDialogButtonBox::Reset)->setToolTip(tr("还原为已保存的设置"));
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setToolTip(tr("将本窗口恢复为默认草稿，保存后生效"));
}

void SettingsDialogButtonBox::settingsWritableChanged(bool writable)
{
  // Hide all first to prevent any change in overall width
  m_lblIcon->setVisible(false);
  m_lblText->setVisible(false);
  m_buttonBox->button(QDialogButtonBox::Save)->setVisible(false);
  m_buttonBox->button(QDialogButtonBox::Reset)->setVisible(false);
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setVisible(false);

  m_lblIcon->setVisible(!writable);
  m_lblText->setVisible(!writable);
  m_buttonBox->button(QDialogButtonBox::Save)->setVisible(writable);
  m_buttonBox->button(QDialogButtonBox::Reset)->setVisible(writable);
  m_buttonBox->button(QDialogButtonBox::RestoreDefaults)->setVisible(writable);
}
