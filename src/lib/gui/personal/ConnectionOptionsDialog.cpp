/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ConnectionOptionsDialog.h"
#include "DialogStyle.h"
#include "ToggleSwitch.h"
#include "gui/core/NetworkMonitor.h"
#include "gui/validators/ComputerNameValidator.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>

namespace deskflow::gui::personal {
namespace {
QVBoxLayout *group(const QString &title, QWidget *body)
{
  auto *card = new QFrame(body);
  card->setProperty("personalCard", true);
  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);
  auto *heading = new QLabel(title, card);
  heading->setProperty("sectionTitle", true);
  layout->addWidget(heading);
  qobject_cast<QVBoxLayout *>(body->layout())->addWidget(card);
  return layout;
}
QLabel *detail(const QString &text, QWidget *parent)
{
  auto *label = new QLabel(text, parent);
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
  label->setProperty("secondaryText", true);
  return label;
}
} // namespace

ConnectionOptionsDialog::ConnectionOptionsDialog(const QStringList &otherComputerNames, QWidget *parent)
    : QDialog(parent),
      m_otherNames(otherComputerNames)
{
  setWindowTitle(tr("连接设置"));
  setObjectName(QStringLiteral("connectionOptionsDialog"));
  prepareDialog(this, 600, 640);
  setMinimumSize(480, 460);
  for (const auto &key :
       {Settings::Core::CoreMode, Settings::Core::ComputerName, Settings::Client::RemoteHost,
        Settings::Client::DynamicConnectionRetry})
    m_baseline.insert(key, Settings::value(key));
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(dialogHeading(tr("连接设置"), tr("确定键鼠由哪台电脑提供，以及此电脑如何连接。"), this));
  auto *body = new QWidget(this);
  auto *bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(24, 0, 24, 22);
  bodyLayout->setSpacing(14);
  auto *identity = group(tr("本机身份"), body);
  auto *nameForm = new QFormLayout;
  nameForm->setHorizontalSpacing(18);
  nameForm->setVerticalSpacing(12);
  m_name = new QLineEdit(m_baseline.value(Settings::Core::ComputerName).toString(), body);
  m_name->setObjectName(QStringLiteral("connectionComputerName"));
  m_name->setMaxLength(255);
  nameForm->addRow(tr("设备名称"), m_name);
  nameForm->addRow(tr("操作系统"), new QLabel(displayOperatingSystem(shortcuts::localOS()), body));
  identity->addLayout(nameForm);
  identity->addWidget(
      detail(tr("名称须与来源电脑中添加的设备一致。可使用字母、数字、点、下划线和连字符，不能包含空格。"), body)
  );
  auto *connection = group(tr("键鼠来源"), body);
  m_role = new QComboBox(body);
  m_role->setObjectName(QStringLiteral("personalComputerRole"));
  m_role->addItem(tr("接收另一台电脑的键鼠"), static_cast<int>(Settings::Client));
  m_role->addItem(tr("共享本机的键盘与鼠标"), static_cast<int>(Settings::Server));
  m_role->setCurrentIndex(qMax(0, m_role->findData(m_baseline.value(Settings::Core::CoreMode).toInt())));
  connection->addWidget(m_role);
  m_explanation = detail({}, body);
  connection->addWidget(m_explanation);
  m_hostRow = new QWidget(body);
  auto *hostLayout = new QVBoxLayout(m_hostRow);
  hostLayout->setContentsMargins(0, 4, 0, 0);
  hostLayout->addWidget(new QLabel(tr("来源电脑地址"), body));
  m_host = new QLineEdit(m_baseline.value(Settings::Client::RemoteHost).toString(), body);
  m_host->setObjectName(QStringLiteral("connectionRemoteHost"));
  m_host->setPlaceholderText(tr("例如 192.168.1.10 或 computer.local"));
  m_host->setClearButtonEnabled(true);
  hostLayout->addWidget(m_host);
  connection->addWidget(m_hostRow);
  auto *recovery = group(tr("断线恢复"), body);
  m_retryCard = recovery->parentWidget();
  m_retry = new ToggleSwitch(tr("连接失败时逐步延长重试间隔"), body);
  m_retry->setObjectName(QStringLiteral("personalDynamicRetry"));
  m_retry->setChecked(m_baseline.value(Settings::Client::DynamicConnectionRetry).toBool());
  recovery->addWidget(m_retry);
  recovery->addWidget(detail(
      tr("共享运行期间会自动尝试重连。开启此项可在持续失败时降低重试频率；关闭时每秒重试。启动时是否自动连接，可在“通用"
         "”中设置。"),
      body
  ));
  m_error = detail({}, body);
  m_error->setObjectName(QStringLiteral("connectionValidation"));
  m_error->setProperty("dangerText", true);
  bodyLayout->addWidget(m_error);
  bodyLayout->addWidget(detail(tr("保存连接参数后，正在运行的共享会短暂重新连接。离线时仅保存设置。"), body));
  bodyLayout->addStretch();
  root->addWidget(dialogScroll(body, this), 1);
  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QHBoxLayout(footer);
  footerLayout->setContentsMargins(24, 14, 24, 16);
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, footer);
  m_save = buttons->button(QDialogButtonBox::Save);
  m_save->setText(tr("保存更改"));
  m_save->setProperty("primaryButton", true);
  buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
  footerLayout->addWidget(buttons);
  root->addWidget(footer);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(m_role, &QComboBox::currentIndexChanged, this, &ConnectionOptionsDialog::refresh);
  connect(m_host, &QLineEdit::textChanged, this, &ConnectionOptionsDialog::refresh);
  connect(m_name, &QLineEdit::textChanged, this, &ConnectionOptionsDialog::refresh);
  connect(Settings::instance(), &Settings::settingsWritableChanged, this, &ConnectionOptionsDialog::refresh);
  refresh();
  applyPersonalStyle(this);
}

void ConnectionOptionsDialog::refresh()
{
  const bool server = m_role->currentData().toInt() == Settings::Server;
  const bool writable = Settings::isWritable();
  for (auto *widget : QList<QWidget *>{m_role, m_host, m_name, m_retry})
    widget->setEnabled(writable);
  m_hostRow->setVisible(!server);
  m_retryCard->setVisible(!server);
  const auto addresses = NetworkMonitor::validAddresses().join(QStringLiteral(" / "));
  m_explanation->setText(
      server ? tr("物理键鼠连接在本机。请在接收电脑中填写本机地址：%1")
                   .arg(addresses.isEmpty() ? tr("暂未发现可用局域网地址") : addresses)
             : tr("物理键鼠连接在来源电脑。快捷键适配由本机处理。")
  );
  const auto name = m_name->text().trimmed();
  validators::ComputerNameValidator validator({});
  QString error;
  if (!writable)
    error = tr("设置为只读，当前只能查看连接信息。");
  else if (name.isEmpty() || !validator.validate(name))
    error = tr("请输入有效的设备名称。");
  else if (!name.front().isLetterOrNumber() || !name.back().isLetterOrNumber())
    error = tr("设备名称须以字母或数字开头和结尾。");
  else if (m_otherNames.contains(name, Qt::CaseInsensitive))
    error = tr("已有另一台设备使用此名称，请更换名称。");
  else if (server && Settings::value(Settings::Server::ExternalConfig).toBool() &&
           name.compare(m_baseline.value(Settings::Core::ComputerName).toString(), Qt::CaseInsensitive) != 0)
    error = tr("当前使用外部配置。请先切回内置配置再修改本机名称，并同步更新外部配置中的设备名称。");
  else if (!server && m_host->text().trimmed().isEmpty())
    error = tr("请填写来源电脑的 IP 地址或主机名。");
  m_error->setText(error);
  m_error->setVisible(!error.isEmpty());
  m_save->setEnabled(error.isEmpty());
}

QVariantMap ConnectionOptionsDialog::changes() const
{
  QVariantMap result;
  const auto add = [&](const QString &key, const QVariant &value) {
    if (value != m_baseline.value(key))
      result.insert(key, value);
  };
  add(Settings::Core::CoreMode, QVariant::fromValue(static_cast<Settings::CoreMode>(m_role->currentData().toInt())));
  add(Settings::Core::ComputerName, m_name->text().trimmed());
  if (m_role->currentData().toInt() == Settings::Client) {
    add(Settings::Client::RemoteHost, m_host->text().trimmed());
    add(Settings::Client::DynamicConnectionRetry, m_retry->isChecked());
  }
  return result;
}
} // namespace deskflow::gui::personal
