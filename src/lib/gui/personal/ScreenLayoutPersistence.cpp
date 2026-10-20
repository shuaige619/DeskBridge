/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "ScreenLayoutPersistence.h"
#include "common/Settings.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

namespace deskflow::gui::personal {
namespace {
bool fail(QString *error, const QString &message)
{
  if (error) *error = message;
  return false;
}

bool writeFile(const QString &path, const QByteArray &contents, QString *error)
{
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    return fail(error, QObject::tr("无法保存屏幕布局：%1").arg(file.errorString()));
  return true;
}

bool saveGrid(ServerConfig &config, QString *error)
{
  config.commit();
  Settings::proxy().sync();
  Settings::save(false);
  Settings::proxy().sync();
  if (Settings::proxy().get().status() != QSettings::NoError)
    return fail(error, QObject::tr("无法保存服务端屏幕设置。"));
  return true;
}
} // namespace

bool ScreenLayoutPersistence::prepare(layout::Direction expected, layout::Direction desired, QString *error)
{
  if (isPending()) return fail(error, QObject::tr("正在应用屏幕布局，请稍候。"));
  const auto actual = m_config.screenLayout();
  if (!actual.editable) return fail(error, actual.error);
  if (actual.direction != expected)
    return fail(error, QObject::tr("服务端布局已改变，请按最新布局重新拖动。"));
  auto candidate = m_config;
  if (!candidate.setScreenLayout(desired, error)) return false;
  return saveCandidate(std::move(candidate), error);
}

bool ScreenLayoutPersistence::prepareMove(const layout::Move &move, QString *error)
{
  if (isPending()) return fail(error, QObject::tr("正在应用屏幕布局，请稍候。"));
  auto candidate = m_config;
  if (!candidate.moveScreen(move, error)) return false;
  return saveCandidate(std::move(candidate), error);
}

bool ScreenLayoutPersistence::saveCandidate(ServerConfig candidate, QString *error)
{

  const auto path = Settings::serverConfigFile();
  QFile previousFile(path);
  const bool existed = previousFile.exists();
  if (existed && !previousFile.open(QIODevice::ReadOnly))
    return fail(error, QObject::tr("无法读取现有屏幕布局：%1").arg(previousFile.errorString()));
  const auto contents = existed ? previousFile.readAll() : QByteArray();
  if (existed && previousFile.error() != QFileDevice::NoError)
    return fail(error, QObject::tr("无法完整读取现有屏幕布局。"));
  previousFile.close();

  QString text;
  QTextStream stream(&text);
  stream << candidate;
  stream.flush();
  if (!writeFile(path, text.toUtf8(), error)) return false;

  m_previous = Previous{m_config, path, contents, existed};
  if (!saveGrid(candidate, error)) {
    QString restoreError;
    finish(false, &restoreError);
    if (error && !restoreError.isEmpty()) *error += QStringLiteral(" ") + restoreError;
    return false;
  }
  m_config = candidate;
  return true;
}

bool ScreenLayoutPersistence::finish(bool applied, QString *error)
{
  if (!m_previous) return true;
  auto previous = std::move(*m_previous);
  m_previous.reset();
  if (applied) return true;

  m_config = previous.config;
  QString gridError;
  const bool gridSaved = saveGrid(m_config, &gridError);
  QString fileError;
  const bool fileSaved = previous.existed ? writeFile(previous.path, previous.contents, &fileError)
                                        : (!QFile::exists(previous.path) || QFile::remove(previous.path));
  if (!gridSaved || !fileSaved)
    return fail(error, QObject::tr("恢复原布局失败：%1 %2").arg(gridError, fileError));
  return true;
}

} // namespace deskflow::gui::personal
