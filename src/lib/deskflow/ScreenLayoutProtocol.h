/*
 * SPDX-FileCopyrightText: (C) 2026 DeskBridge contributors
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#pragma once

#include "base/Event.h"
#include "common/ScreenLayout.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

namespace deskflow::layout {

inline int capabilityVersion(const OptionsList &options)
{
  if (options.size() % 2) return 0;
  int version = 0;
  for (size_t i = 0; i < options.size(); i += 2) {
    if (options[i] == kOptionDeskBridgeLayout) {
      if (version || options[i + 1] < 1 || options[i + 1] > 2) return 0;
      version = static_cast<int>(options[i + 1]);
    }
  }
  return version;
}

// Retain the v1 feature probe for legacy callers. v2 uses capabilityVersion.
inline bool hasCapability(const OptionsList &options) { return capabilityVersion(options) == 1; }

inline constexpr uint32_t kMaxWirePayloadLength = 96 * 1024;
inline QJsonObject readWireObject(deskflow::IStream *stream)
{
  uint32_t size = 0;
  if (!ProtocolUtil::readf(stream, "%4i", &size) || size == 0 || size > kMaxWirePayloadLength)
    throw BadClientException("layout payload exceeds maximum size");
  QByteArray bytes(static_cast<qsizetype>(size), '\0');
  uint32_t offset = 0;
  while (offset < size) {
    const auto count = stream->read(bytes.data() + offset, size - offset);
    if (!count) throw BadClientException("incomplete layout payload");
    offset += count;
  }
  QJsonParseError error;
  const auto document = QJsonDocument::fromJson(bytes, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject())
    throw BadClientException("invalid layout JSON");
  return document.object();
}

inline void writeWireObject(deskflow::IStream *stream, const char *message, const QJsonObject &object)
{
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
  if (bytes.isEmpty() || bytes.size() > kMaxWirePayloadLength)
    throw BadClientException("layout payload exceeds maximum size");
  const auto text = bytes.toStdString();
  ProtocolUtil::writef(stream, message, &text);
}

struct Command : EventData
{
  enum class Kind { Query, Request, Move, Complete };
  Kind kind = Kind::Query;
  quint32 id = 0;
  Direction expected = Direction::Unknown;
  Direction desired = Direction::Unknown;
  deskflow::layout::Move move;
  bool success = false;
  bool controllerAvailable = false;
  QString error;
};

// Request/subscribe frames contain three bounded integers. A zero request is
// only a subscription; every mutation requires both cardinal directions.
inline bool validRequest(quint32 id, quint32 expected, quint32 desired)
{
  return (id == 0 && expected == 0 && desired == 0) ||
         (id != 0 && isCardinal(static_cast<Direction>(expected)) &&
          isCardinal(static_cast<Direction>(desired)));
}

} // namespace deskflow::layout
