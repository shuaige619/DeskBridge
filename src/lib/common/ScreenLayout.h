/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace deskflow::layout {

// The client's position relative to the keyboard/mouse source (server).
enum class Direction { Unknown = 0, Left = 1, Right = 2, Above = 3, Below = 4 };

inline bool isCardinal(Direction value)
{
  return value >= Direction::Left && value <= Direction::Below;
}

inline Direction opposite(Direction value)
{
  switch (value) {
  case Direction::Left: return Direction::Right;
  case Direction::Right: return Direction::Left;
  case Direction::Above: return Direction::Below;
  case Direction::Below: return Direction::Above;
  default: return Direction::Unknown;
  }
}

struct Screen
{
  QString name;
  int column = 0;
  int row = 0;
  bool operator==(const Screen &) const = default;
};

struct Move
{
  QString screenName;
  int column = 0;
  int row = 0;
  QString expectedRevision;
  bool operator==(const Move &) const = default;
};

struct Snapshot
{
  QString serverName;
  QString clientName;
  Direction direction = Direction::Unknown;
  bool editable = false;
  QString error;
  QList<Screen> screens;
  QString revision;
  QString localName;
  int protocolVersion = 1;
  QStringList connectedNames;
  bool operator==(const Snapshot &) const = default;
};

inline constexpr int kMaxNameLength = 255; // UTF-8 bytes, including v2 transport bounds.
inline constexpr int kMaxErrorLength = 512;
inline constexpr int kMaxScreens = 64;
inline constexpr int kMaxCoordinate = 64;
inline constexpr int kMaxIpcPayloadLength = 192 * 1024;

namespace topology_detail {
inline bool fail(QString *error, const QString &message)
{
  if (error) *error = message;
  return false;
}

inline bool validName(const QString &name, bool allowEmpty = false)
{
  if (name.isEmpty()) return allowEmpty;
  if (name.size() > kMaxNameLength || name.toUtf8().size() > kMaxNameLength) return false;
  for (qsizetype i = 0; i < name.size(); ++i) {
    const auto ch = name[i];
    if (ch.isHighSurrogate()) {
      if (++i >= name.size() || !name[i].isLowSurrogate()) return false;
    } else if (ch.isLowSurrogate() || ch.category() == QChar::Other_Control) return false;
  }
  return true;
}

inline bool validRevision(const QString &revision)
{
  if (revision.size() != 64) return false;
  for (const auto ch : revision)
    if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f'))) return false;
  return true;
}

inline QList<Screen> sortedScreens(QList<Screen> screens)
{
  std::sort(screens.begin(), screens.end(), [](const Screen &a, const Screen &b) { return a.name < b.name; });
  return screens;
}

inline bool screenShape(const Snapshot &snapshot, QString *error)
{
  if (!validName(snapshot.serverName) || snapshot.screens.isEmpty() || snapshot.screens.size() > kMaxScreens)
    return fail(error, QObject::tr("屏幕布局缺少来源电脑或超过设备数量限制。"));
  QSet<QString> names;
  QSet<QPair<int, int>> cells;
  bool sourceFound = false;
  for (const auto &screen : snapshot.screens) {
    if (!validName(screen.name)) return fail(error, QObject::tr("屏幕名称无效或过长。"));
    if (screen.column < -kMaxCoordinate || screen.column > kMaxCoordinate ||
        screen.row < -kMaxCoordinate || screen.row > kMaxCoordinate)
      return fail(error, QObject::tr("屏幕位置超出允许的布局范围。"));
    if (names.contains(screen.name)) return fail(error, QObject::tr("屏幕名称重复，请检查来源端配置。"));
    const QPair<int, int> cell{screen.column, screen.row};
    if (cells.contains(cell)) return fail(error, QObject::tr("多台电脑占用了同一个屏幕位置。"));
    names.insert(screen.name);
    cells.insert(cell);
    if (screen.name == snapshot.serverName) {
      sourceFound = true;
      if (screen.column != 0 || screen.row != 0)
        return fail(error, QObject::tr("屏幕布局的来源电脑必须位于原点。"));
    }
  }
  if (!sourceFound) return fail(error, QObject::tr("屏幕布局中没有键鼠来源电脑。"));
  return true;
}

inline std::optional<int> integer(const QJsonValue &value, int minimum, int maximum)
{
  if (!value.isDouble()) return std::nullopt;
  const auto number = value.toDouble();
  if (number < minimum || number > maximum || number != static_cast<int>(number)) return std::nullopt;
  return static_cast<int>(number);
}

inline void updatePairFields(Snapshot &snapshot)
{
  snapshot.clientName.clear();
  snapshot.direction = Direction::Unknown;
  if (snapshot.screens.size() != 2) return;
  for (const auto &screen : snapshot.screens) {
    if (screen.name == snapshot.serverName) continue;
    snapshot.clientName = screen.name;
    if (screen.column == -1 && screen.row == 0) snapshot.direction = Direction::Left;
    else if (screen.column == 1 && screen.row == 0) snapshot.direction = Direction::Right;
    else if (screen.column == 0 && screen.row == -1) snapshot.direction = Direction::Above;
    else if (screen.column == 0 && screen.row == 1) snapshot.direction = Direction::Below;
  }
}
} // namespace topology_detail

// Status and receiver-specific fields never affect the compare-and-swap revision.
inline QString revisionFor(const Snapshot &snapshot)
{
  QJsonArray screens;
  for (const auto &screen : topology_detail::sortedScreens(snapshot.screens))
    screens.append(QJsonArray{screen.name, screen.column, screen.row});
  const auto canonical = QJsonDocument(QJsonArray{snapshot.serverName, screens}).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

inline bool validateTopology(const Snapshot &snapshot, QString *error = nullptr)
{
  if (!topology_detail::screenShape(snapshot, error)) return false;
  QSet<qsizetype> visited{0};
  QList<qsizetype> queue{0};
  for (qsizetype next = 0; next < queue.size(); ++next) {
    const auto &from = snapshot.screens[queue[next]];
    for (qsizetype i = 0; i < snapshot.screens.size(); ++i) {
      const auto &to = snapshot.screens[i];
      if (!visited.contains(i) && std::abs(from.column - to.column) + std::abs(from.row - to.row) == 1) {
        visited.insert(i);
        queue.append(i);
      }
    }
  }
  if (visited.size() != snapshot.screens.size())
    return topology_detail::fail(error, QObject::tr("屏幕必须通过相邻的上、下、左、右位置连通；此移动会断开电脑。"));
  if (error) error->clear();
  return true;
}

inline std::optional<Snapshot> movedSnapshot(const Snapshot &current, const Move &move, QString *error = nullptr)
{
  if (!validateTopology(current, error)) return std::nullopt;
  const auto revision = revisionFor(current);
  if (current.revision != revision || move.expectedRevision != revision) {
    topology_detail::fail(error, QObject::tr("屏幕布局已改变，请按最新布局重新拖动。"));
    return std::nullopt;
  }
  if (!topology_detail::validName(move.screenName) || move.column < -kMaxCoordinate || move.column > kMaxCoordinate ||
      move.row < -kMaxCoordinate || move.row > kMaxCoordinate) {
    topology_detail::fail(error, QObject::tr("屏幕名称或目标位置无效。"));
    return std::nullopt;
  }
  Snapshot result = current;
  bool found = false;
  for (auto &screen : result.screens) {
    if (screen.name == move.screenName) {
      screen.column = move.column;
      screen.row = move.row;
      found = true;
    } else if (screen.column == move.column && screen.row == move.row) {
      topology_detail::fail(error, QObject::tr("该位置已有电脑，请选择空白位置。"));
      return std::nullopt;
    }
  }
  if (!found) {
    topology_detail::fail(error, QObject::tr("要移动的电脑已不在当前屏幕布局中。"));
    return std::nullopt;
  }
  if (move.screenName == current.serverName)
    for (auto &screen : result.screens) { screen.column -= move.column; screen.row -= move.row; }
  if (!validateTopology(result, error)) return std::nullopt;
  result.screens = topology_detail::sortedScreens(result.screens);
  topology_detail::updatePairFields(result);
  result.revision = revisionFor(result);
  return result;
}

// JSON is carried as hex in line-based IPC: names/errors cannot inject a new
// command or the '=' separator, and payloads have a small fixed upper bound.
inline QString encodeIpc(const QJsonObject &object)
{
  return QString::fromLatin1(QJsonDocument(object).toJson(QJsonDocument::Compact).toHex());
}

inline std::optional<QJsonObject> decodeIpc(const QString &text)
{
  if (text.isEmpty() || text.size() > kMaxIpcPayloadLength || text.size() % 2)
    return std::nullopt;
  for (const auto ch : text) {
    if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f')))
      return std::nullopt;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(QByteArray::fromHex(text.toLatin1()), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    return std::nullopt;
  return document.object();
}

inline QJsonObject toJson(const Snapshot &snapshot)
{
  QJsonArray screens;
  for (const auto &screen : snapshot.screens)
    screens.append(QJsonObject{{"name", screen.name}, {"column", screen.column}, {"row", screen.row}});
  QJsonArray connected;
  for (const auto &name : snapshot.connectedNames) connected.append(name);
  return {{"server", snapshot.serverName}, {"client", snapshot.clientName},
          {"direction", static_cast<int>(snapshot.direction)}, {"editable", snapshot.editable},
          {"error", snapshot.error.left(kMaxErrorLength)}, {"screens", screens}, {"revision", snapshot.revision},
          {"localName", snapshot.localName}, {"protocolVersion", snapshot.protocolVersion}, {"connectedNames", connected}};
}

inline std::optional<Snapshot> snapshotFromJson(const QJsonObject &object)
{
  const auto direction = topology_detail::integer(object.value("direction"), 0, 4);
  if (!direction || !object.value("server").isString() || !object.value("client").isString() ||
      !object.value("editable").isBool() || !object.value("error").isString()) return std::nullopt;
  Snapshot result{object.value("server").toString(), object.value("client").toString(),
                  static_cast<Direction>(*direction), object.value("editable").toBool(),
                  object.value("error").toString()};
  if (!topology_detail::validName(result.serverName, true) || !topology_detail::validName(result.clientName, true) ||
      result.error.size() > kMaxErrorLength) return std::nullopt;
  if (object.contains("protocolVersion")) {
    const auto version = topology_detail::integer(object.value("protocolVersion"), 1, 2);
    if (!version) return std::nullopt;
    result.protocolVersion = *version;
  }
  if (object.contains("screens")) {
    if (!object.value("screens").isArray()) return std::nullopt;
    const auto screens = object.value("screens").toArray();
    if (screens.size() > kMaxScreens) return std::nullopt;
    for (const auto &value : screens) {
      if (!value.isObject()) return std::nullopt;
      const auto screen = value.toObject();
      const auto column = topology_detail::integer(screen.value("column"), -kMaxCoordinate, kMaxCoordinate);
      const auto row = topology_detail::integer(screen.value("row"), -kMaxCoordinate, kMaxCoordinate);
      if (!screen.value("name").isString() || !column || !row) return std::nullopt;
      result.screens.append({screen.value("name").toString(), *column, *row});
    }
  }
  if (object.contains("revision")) {
    if (!object.value("revision").isString()) return std::nullopt;
    result.revision = object.value("revision").toString();
  }
  if (object.contains("localName")) {
    if (!object.value("localName").isString()) return std::nullopt;
    result.localName = object.value("localName").toString();
    if (!topology_detail::validName(result.localName, true)) return std::nullopt;
  }
  if (object.contains("connectedNames")) {
    if (!object.value("connectedNames").isArray() || object.value("connectedNames").toArray().size() > kMaxScreens)
      return std::nullopt;
    for (const auto &value : object.value("connectedNames").toArray()) {
      if (!value.isString() || !topology_detail::validName(value.toString()) || result.connectedNames.contains(value.toString()))
        return std::nullopt;
      result.connectedNames.append(value.toString());
    }
  }
  if (result.protocolVersion == 1) {
    if (!result.screens.isEmpty() || !result.revision.isEmpty() ||
        (result.editable && (!isCardinal(result.direction) || result.serverName.isEmpty() || result.clientName.isEmpty())))
      return std::nullopt;
  } else if (result.screens.isEmpty()) {
    // An unsupported custom topology may still report actual connected names.
    if (result.editable || !result.revision.isEmpty()) return std::nullopt;
  } else {
    if (!topology_detail::screenShape(result, nullptr) || !topology_detail::validRevision(result.revision) ||
        result.revision != revisionFor(result) || (result.editable && !validateTopology(result))) return std::nullopt;
    QSet<QString> names;
    for (const auto &screen : result.screens) names.insert(screen.name);
    if (!result.localName.isEmpty() && !names.contains(result.localName)) return std::nullopt;
    for (const auto &name : result.connectedNames) if (!names.contains(name)) return std::nullopt;
  }
  return result;
}

inline QJsonObject moveToJson(const Move &move)
{
  return {{"screenName", move.screenName}, {"column", move.column}, {"row", move.row}, {"expectedRevision", move.expectedRevision}};
}

inline std::optional<Move> moveFromJson(const QJsonObject &object)
{
  const auto column = topology_detail::integer(object.value("column"), -kMaxCoordinate, kMaxCoordinate);
  const auto row = topology_detail::integer(object.value("row"), -kMaxCoordinate, kMaxCoordinate);
  if (!column || !row || !object.value("screenName").isString() || !object.value("expectedRevision").isString())
    return std::nullopt;
  Move result{object.value("screenName").toString(), *column, *row, object.value("expectedRevision").toString()};
  if (!topology_detail::validName(result.screenName) || !topology_detail::validRevision(result.expectedRevision)) return std::nullopt;
  return result;
}

inline std::optional<quint32> requestIdFromJson(const QJsonObject &object)
{
  const auto value = object.value("id");
  const auto number = value.toDouble(-1);
  if (!value.isDouble() || number < 1 || number > 0xffffffffu || number != static_cast<quint32>(number))
    return std::nullopt;
  return static_cast<quint32>(number);
}

} // namespace deskflow::layout

Q_DECLARE_METATYPE(deskflow::layout::Direction)
Q_DECLARE_METATYPE(deskflow::layout::Screen)
Q_DECLARE_METATYPE(deskflow::layout::Move)
Q_DECLARE_METATYPE(deskflow::layout::Snapshot)
