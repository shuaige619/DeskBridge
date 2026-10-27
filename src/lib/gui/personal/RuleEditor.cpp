/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "RuleEditor.h"
#include "ApplicationPicker.h"
#include "ChordRecorder.h"
#include "DialogStyle.h"
#include "PersonalStyle.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QUuid>
#include <QVBoxLayout>

namespace deskflow::gui::personal {

RuleEditor::RuleEditor(
    const shortcuts::Configuration &configuration, const shortcuts::Rule &rule, QWidget *parent,
    shortcuts::DeviceOS detectedSourceOS
)
    : QDialog(parent),
      m_configuration(configuration),
      m_original(rule),
      m_apps(configuration.apps),
      m_sourceOS(shortcuts::effectiveSourceOS(configuration, detectedSourceOS))
{
  const bool existing = !rule.id.isEmpty();
  if (!existing)
    m_original.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
  setObjectName(QStringLiteral("ruleEditor"));
  setWindowTitle(existing ? tr("编辑映射规则") : tr("添加映射规则"));
  // Cocoa presents a window-modal Dialog as a sheet. OR-ing Qt::Sheet into
  // Dialog changes its type to Drawer, whose native panel cannot receive keys.
  setWindowModality(Qt::WindowModal);
  resize(570, 620);
  auto *outer = new QVBoxLayout(this);
  outer->setContentsMargins(24, 24, 24, 20);
  outer->setSpacing(16);
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *content = new QWidget(scroll);
  auto *body = new QVBoxLayout(content);
  body->setContentsMargins(0, 0, 8, 0);
  body->setSpacing(16);
  auto *intro = new QLabel(tr("将远程键盘的组合键转换为本机快捷键。本机物理键盘不受影响。"), content);
  intro->setProperty("secondaryText", true);
  intro->setWordWrap(true);
  body->addWidget(intro);
  auto *form = new QFormLayout;
  form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
  m_name = new QLineEdit(m_original.name, content);
  m_name->setObjectName(QStringLiteral("ruleName"));
  m_name->setAccessibleName(tr("规则名称"));
  m_name->setPlaceholderText(tr("例如：复制"));
  m_name->setMaxLength(80);
  form->addRow(tr("名称"), m_name);
  body->addLayout(form);

  auto *chords = new QHBoxLayout;
  auto *inputColumn = new QVBoxLayout;
  inputColumn->addWidget(new QLabel(tr("远程键盘输入"), content));
  m_input = new ChordRecorder(content);
  m_input->setObjectName(QStringLiteral("inputChord"));
  m_input->setAccessibleName(tr("录入原快捷键"));
  m_input->setChord(m_original.input);
  m_input->setDisplayOperatingSystem(m_sourceOS);
  inputColumn->addWidget(m_input);
  chords->addLayout(inputColumn, 1);
  chords->addWidget(new QLabel(QStringLiteral("→"), content), 0, Qt::AlignBottom);
  auto *outputColumn = new QVBoxLayout;
  outputColumn->addWidget(new QLabel(tr("本机收到"), content));
  m_output = new ChordRecorder(content);
  m_output->setObjectName(QStringLiteral("outputChord"));
  m_output->setAccessibleName(tr("录入目标快捷键"));
  m_output->setChord(m_original.output);
  outputColumn->addWidget(m_output);
  chords->addLayout(outputColumn, 1);
  body->addLayout(chords);
  auto *recordingHint = new QLabel(tr("点按录入框后按组合键，Esc 取消录入。支持字母、数字、功能键与导航键。"), content);
  recordingHint->setWordWrap(true);
  recordingHint->setProperty("secondaryText", true);
  body->addWidget(recordingHint);

  auto *appsHeading = new QHBoxLayout;
  auto *appsLabel = new QLabel(tr("适用应用"), content);
  appsLabel->setProperty("sectionTitle", true);
  appsHeading->addWidget(appsLabel);
  appsHeading->addStretch();
  auto *addApp = new QPushButton(tr("添加应用…"), content);
  addApp->setObjectName(QStringLiteral("addRuleApplication"));
  addApp->setAccessibleName(tr("为此规则添加应用"));
  appsHeading->addWidget(addApp);
  body->addLayout(appsHeading);
  m_appChoices = new QWidget(content);
  m_appChoices->setObjectName(QStringLiteral("ruleApplications"));
  m_appLayout = new QVBoxLayout(m_appChoices);
  m_appLayout->setContentsMargins(0, 0, 0, 0);
  body->addWidget(m_appChoices);
  auto *appsHint = new QLabel(tr("仅在总应用范围内生效。新添加的应用会加入总范围，默认只用于本规则。"), content);
  appsHint->setProperty("secondaryText", true);
  appsHint->setWordWrap(true);
  body->addWidget(appsHint);
  auto *previewHeading = new QLabel(tr("规则预览"), content);
  previewHeading->setProperty("sectionTitle", true);
  body->addWidget(previewHeading);
  m_preview = new QLabel(content);
  m_preview->setObjectName(QStringLiteral("rulePreview"));
  m_preview->setWordWrap(true);
  m_preview->setProperty("personalCard", true);
  m_preview->setMargin(14);
  body->addWidget(m_preview);
  auto *previewHint = new QLabel(tr("仅计算这条规则的效果，不向应用发送按键，也不读取输入内容。"), content);
  previewHint->setProperty("secondaryText", true);
  previewHint->setWordWrap(true);
  body->addWidget(previewHint);
  m_terminalWarning = new QLabel(
#ifdef Q_OS_MACOS
      tr("Codex 内置终端也会使用这条规则。若需保留 Ctrl+C 中断命令，请取消勾选 Codex。"), content
#else
      tr("规则按整个应用生效，内置终端也会使用此转换。需要原始终端快捷键时，请取消勾选该应用。"), content
#endif
  );
  m_terminalWarning->setObjectName(QStringLiteral("editorTerminalWarning"));
  m_terminalWarning->setWordWrap(true);
  body->addWidget(m_terminalWarning);
  m_error = new QLabel(content);
  m_error->setObjectName(QStringLiteral("ruleValidationError"));
  m_error->setProperty("dangerText", true);
  m_error->setWordWrap(true);
  body->addWidget(m_error);
  body->addStretch();
  scroll->setWidget(content);
  outer->addWidget(scroll, 1);

  auto *footer = new QHBoxLayout;
  if (existing) {
    auto *remove = new QPushButton(tr("删除规则…"), this);
    remove->setObjectName(QStringLiteral("deleteRule"));
    footer->addWidget(remove);
    connect(remove, &QPushButton::clicked, this, [this] {
      if (QMessageBox::question(
              this, tr("删除规则"), tr("从待应用的配置中删除这条规则？"), QMessageBox::Cancel | QMessageBox::Yes,
              QMessageBox::Cancel
          ) == QMessageBox::Yes) {
        m_delete = true;
        QDialog::accept();
      }
    });
  }
  footer->addStretch();
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save, this);
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  m_save = buttons->button(QDialogButtonBox::Save);
  m_save->setText(tr("保存到草稿"));
  m_save->setObjectName(QStringLiteral("saveRule"));
  m_save->setProperty("primaryButton", true);
  footer->addWidget(buttons);
  outer->addLayout(footer);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(buttons, &QDialogButtonBox::accepted, this, &RuleEditor::accept);
  connect(m_name, &QLineEdit::textChanged, this, &RuleEditor::updatePreview);
  connect(m_input, &ChordRecorder::chordChanged, this, &RuleEditor::updatePreview);
  connect(m_output, &ChordRecorder::chordChanged, this, &RuleEditor::updatePreview);
  connect(addApp, &QPushButton::clicked, this, [this] {
    // Preserve the current subset before rebuilding its controls.
    m_original = this->rule();
    ApplicationPicker picker(m_apps, this);
    if (picker.exec() != QDialog::Accepted)
      return;
    for (const auto &app : picker.selectedApplications()) {
      if (!m_apps.contains(app)) {
        m_apps.append(app);
        m_original.apps.append(app);
      }
    }
    rebuildApplications();
    updatePreview();
  });
  rebuildApplications();
  updatePreview();
  prepareDialog(this, width(), height());
  m_name->setFocus();
}

void RuleEditor::rebuildApplications()
{
  m_appChecks.clear();
  while (auto *item = m_appLayout->takeAt(0)) {
    delete item->widget();
    delete item;
  }
  for (const auto &id : m_apps) {
    const auto app = ApplicationPicker::application(id);
    auto *check = new QCheckBox(app.name, m_appChoices);
    check->setIcon(app.icon);
    check->setIconSize(QSize(24, 24));
    check->setObjectName(QStringLiteral("ruleApp_%1").arg(id));
    check->setAccessibleName(tr("此规则用于 %1").arg(app.name));
    check->setProperty("applicationId", id);
    check->setChecked(m_original.apps.contains(id));
    m_appLayout->addWidget(check);
    m_appChecks.append(check);
    connect(check, &QCheckBox::toggled, this, &RuleEditor::updatePreview);
  }
  if (m_apps.isEmpty()) {
    auto *empty = new QLabel(tr("先添加至少一个生效应用。"), m_appChoices);
    empty->setProperty("secondaryText", true);
    m_appLayout->addWidget(empty);
  }
}

shortcuts::Rule RuleEditor::rule() const
{
  auto result = m_original;
  result.name = m_name->text().trimmed();
  result.input = m_input->chord();
  result.output = m_output->chord();
  // Keep dormant app selections outside the current global boundary.
  for (const auto &app : m_apps)
    result.apps.removeAll(app);
  for (const auto *check : m_appChecks)
    if (check->isChecked())
      result.apps.append(check->property("applicationId").toString());
  result.apps.removeDuplicates();
  return result;
}

QStringList RuleEditor::globalApplications() const
{
  return m_apps;
}

bool RuleEditor::deletionRequested() const
{
  return m_delete;
}

QString RuleEditor::validationError() const
{
  const auto candidate = rule();
  if (candidate.name.isEmpty())
    return tr("请输入规则名称。");
  if (candidate.input.key == kKeyNone)
    return tr("请录入远程键盘输入的快捷键。");
  if (candidate.output.key == kKeyNone)
    return tr("请录入本机应收到的快捷键。");
  bool hasApp = false;
  for (const auto *check : m_appChecks)
    hasApp |= check->isChecked();
  if (!hasApp)
    return tr("请至少选择一个当前生效的应用。");
  auto config = m_configuration;
  config.apps = m_apps;
  bool replaced = false;
  for (auto &entry : config.rules) {
    if (entry.id == candidate.id) {
      entry = candidate;
      replaced = true;
      break;
    }
  }
  if (!replaced)
    config.rules.append(candidate);
  return shortcuts::validate(config);
}

void RuleEditor::updatePreview()
{
  QStringList names;
  for (const auto *check : m_appChecks)
    if (check->isChecked())
      names.append(check->text());
  const auto candidate = rule();
  m_preview->setText(tr("在 %1 中：\n%2  →  %3")
                         .arg(
                             names.isEmpty() ? tr("所选应用") : names.join(tr("、")),
                             shortcuts::chordText(candidate.input, m_sourceOS), shortcuts::chordText(candidate.output)
                         ));
  m_terminalWarning->setVisible(
      shortcuts::localOS() == shortcuts::DeviceOS::MacOS
          ? candidate.apps.contains(QStringLiteral("com.openai.codex")) &&
                m_apps.contains(QStringLiteral("com.openai.codex"))
          : !names.isEmpty()
  );
  const auto error = validationError();
  m_error->setText(error);
  m_error->setVisible(!error.isEmpty());
  m_save->setEnabled(error.isEmpty());
}

void RuleEditor::accept()
{
  updatePreview();
  if (validationError().isEmpty())
    QDialog::accept();
}

} // namespace deskflow::gui::personal
