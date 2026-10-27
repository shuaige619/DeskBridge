/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
// 桌桥证书界面重设计，2026-10-21；信任决定仍由显式接受 / 取消返回。

#include "FingerprintDialog.h"
#include "net/SecureUtils.h"
#include "personal/DialogStyle.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
class FingerprintArt final : public QWidget
{
public:
  explicit FingerprintArt(const QString &art, QWidget *parent) : QWidget(parent), m_lines(art.split('\n'))
  {
    setAccessibleName(QObject::tr("证书图形指纹，辅助核对 SHA-256"));
    setMinimumSize(230, 170);
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setKerning(false);
    painter.setFont(font);
    painter.setPen(palette().color(QPalette::WindowText));
    if (m_lines.size() == 1 && m_lines.first().isEmpty()) {
      painter.drawText(rect(), Qt::AlignCenter, QObject::tr("证书指纹不可用"));
      return;
    }
    const auto metrics = painter.fontMetrics();
    const int cellWidth = qMax(9, metrics.horizontalAdvance(QLatin1Char('M')));
    const int cellHeight = metrics.height();
    int columns = 0;
    for (const auto &line : m_lines)
      columns = qMax(columns, static_cast<int>(line.size()));
    const QPoint origin((width() - columns * cellWidth) / 2, (height() - m_lines.size() * cellHeight) / 2);
    // A fixed grid also keeps Unicode frame characters aligned when the
    // platform substitutes a font for individual glyphs.
    for (int row = 0; row < m_lines.size(); ++row)
      for (int column = 0; column < m_lines[row].size(); ++column)
        painter.drawText(
            QRect(origin + QPoint(column * cellWidth, row * cellHeight), QSize(cellWidth, cellHeight)), Qt::AlignCenter,
            QString(m_lines[row][column])
        );
  }

private:
  QStringList m_lines;
};

QWidget *
fingerprintCard(QWidget *parent, const Fingerprint &fingerprint, const QString &title, QStackedWidget *&preview)
{
  auto *card = new QWidget(parent);
  card->setProperty("personalCard", true);
  auto *layout = new QVBoxLayout(card);
  layout->setContentsMargins(18, 18, 18, 18);
  layout->setSpacing(12);
  auto *heading = new QLabel(title, card);
  heading->setProperty("sectionTitle", true);
  heading->setWordWrap(true);
  layout->addWidget(heading);
  auto *algorithm = new QLabel(QStringLiteral("SHA-256"), card);
  algorithm->setProperty("secondaryText", true);
  layout->addWidget(algorithm);

  preview = new QStackedWidget(card);
  const bool valid = fingerprint.isValid() && fingerprint.type == QCryptographicHash::Sha256;
  auto *art = new FingerprintArt(valid ? deskflow::generateFingerprintArt(fingerprint.data) : QString(), preview);
  auto *hash = new QLabel(
      valid ? deskflow::formatSSLFingerprintColumns(fingerprint.data) : QObject::tr("证书指纹不可用"), preview
  );
  preview->addWidget(art);
  for (auto *label : {hash}) {
    label->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    label->setAlignment(Qt::AlignCenter);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setMargin(8);
    preview->addWidget(label);
  }
  preview->setCurrentIndex(1);
  layout->addWidget(preview, 1);
  auto *copy = new QPushButton(QObject::tr("复制指纹"), card);
  copy->setEnabled(valid);
  copy->setAccessibleName(QObject::tr("复制%1的 SHA-256 指纹").arg(title));
  QObject::connect(copy, &QPushButton::clicked, card, [fingerprint, copy] {
    QApplication::clipboard()->setText(QString::fromLatin1(fingerprint.data.toHex(':').toUpper()));
    copy->setText(QObject::tr("已复制指纹"));
  });
  layout->addWidget(copy);
  return card;
}
} // namespace

FingerprintDialog::FingerprintDialog(
    QWidget *parent, const Fingerprint &localFingerprint, FingerprintDialogMode mode,
    const Fingerprint &remoteFingerprint
)
    : QDialog(parent),
      m_lblHeader{new QLabel(this)},
      m_lblFooter{new QLabel(this)},
      m_buttonBox{new QDialogButtonBox(QDialogButtonBox::Help, this)}
{
  using namespace deskflow::gui::personal;
  const bool localMode = mode == FingerprintDialogMode::Local;
  const bool isServer = mode == FingerprintDialogMode::Server;
  prepareDialog(this, localMode ? 510 : 760, localMode ? 540 : 590);
  setWindowTitle(localMode ? tr("本机证书指纹") : tr("确认连接设备"));
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(dialogHeading(
      localMode ? tr("本机证书指纹") : tr("确认对方身份"),
      localMode ? tr("在另一台电脑的连接确认窗口中，核对这里的指纹。")
                : tr("首次连接需要核对证书。请先在对方电脑上查看「本机证书指纹」。"),
      this
  ));
  auto *content = new QWidget(this);
  auto *contentLayout = new QVBoxLayout(content);
  contentLayout->setContentsMargins(24, 0, 24, 18);
  contentLayout->setSpacing(16);
  m_lblHeader->setWordWrap(true);
  m_lblHeader->setProperty("secondaryText", true);
  m_lblHeader->setText(
      localMode ? tr("指纹是这台电脑的证书标识，不包含私钥。")
                : tr("「对方」指纹须与对方电脑显示的本机指纹完全一致。"
                     "每台电脑的证书不同，这里左右两个指纹通常也不同。")
  );
  contentLayout->addWidget(m_lblHeader);
  contentLayout->addLayout(
      localMode ? makeLocalLayout(localFingerprint) : makeCompareLayout(localFingerprint, isServer, remoteFingerprint)
  );
  m_lblFooter->setWordWrap(true);
  m_lblFooter->setText(
      localMode ? tr("证书重新生成后，其他电脑需要重新确认并信任新的指纹。")
                : tr("只有核对一致且认识这台电脑时，才信任并连接；指纹不一致请取消。")
  );
  m_lblFooter->setProperty(localMode ? "secondaryText" : "dangerText", true);
  contentLayout->addWidget(m_lblFooter);
  layout->addWidget(dialogScroll(content, this), 1);

  auto *footer = new QWidget(this);
  footer->setProperty("personalFooter", true);
  auto *footerLayout = new QVBoxLayout(footer);
  footerLayout->setContentsMargins(24, 14, 24, 14);
  footerLayout->addWidget(m_buttonBox);
  layout->addWidget(footer);
  if (localMode) {
    auto *done = m_buttonBox->addButton(QDialogButtonBox::Ok);
    done->setText(tr("完成"));
    done->setProperty("primaryButton", true);
    done->setDefault(true);
    connect(done, &QPushButton::clicked, this, &QDialog::accept);
  } else {
    auto *trust = m_buttonBox->addButton(QDialogButtonBox::Yes);
    trust->setText(tr("信任并连接"));
    trust->setProperty("primaryButton", true);
    trust->setAutoDefault(false);
    trust->setEnabled(
        localFingerprint.isValid() && remoteFingerprint.isValid() &&
        localFingerprint.type == QCryptographicHash::Sha256 && remoteFingerprint.type == QCryptographicHash::Sha256
    );
    auto *cancel = m_buttonBox->addButton(QDialogButtonBox::No);
    cancel->setText(tr("取消连接"));
    cancel->setDefault(true);
    cancel->setFocus();
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(trust, &QPushButton::clicked, this, &QDialog::accept);
  }
  auto *modeButton = m_buttonBox->button(QDialogButtonBox::Help);
  modeButton->setCheckable(true);
  modeButton->setChecked(true);
  modeButton->setAutoDefault(false);
  updateModeButton(true);
  connect(modeButton, &QPushButton::toggled, this, &FingerprintDialog::togglePreviewMode);
  applyPersonalStyle(this);
}

QLayout *FingerprintDialog::makeLocalLayout(const Fingerprint &localFingerprint)
{
  auto *layout = new QVBoxLayout;
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(fingerprintCard(this, localFingerprint, tr("本机"), m_localPreview));
  return layout;
}

QLayout *FingerprintDialog::makeCompareLayout(
    const Fingerprint &localFingerprint, bool isServer, const Fingerprint &remoteFingerprint
)
{
  auto *layout = new QHBoxLayout;
  layout->setSpacing(16);
  layout->addWidget(
      fingerprintCard(this, localFingerprint, isServer ? tr("本机 · 来源端") : tr("本机 · 接收端"), m_localPreview)
  );
  layout->addWidget(
      fingerprintCard(this, remoteFingerprint, isServer ? tr("对方 · 接收端") : tr("对方 · 来源端"), m_remotePreview)
  );
  return layout;
}

void FingerprintDialog::togglePreviewMode(bool hashMode)
{
  m_localPreview->setCurrentIndex(hashMode ? 1 : 0);
  if (m_remotePreview)
    m_remotePreview->setCurrentIndex(hashMode ? 1 : 0);
  updateModeButton(hashMode);
}

void FingerprintDialog::updateModeButton(bool hashMode) const
{
  auto *button = m_buttonBox->button(QDialogButtonBox::Help);
  button->setText(hashMode ? tr("查看图形指纹") : tr("查看 SHA-256 指纹"));
  button->setToolTip(hashMode ? tr("切换到图形对照，方便目视核验") : tr("切换到完整 SHA-256 数值进行核验"));
}
