/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MockEventQueue.h"
#include "deskflow/PacketStreamFilter.h"
#include "io/IOException.h"

#include <QByteArray>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

namespace {

// Only stores bytes in memory; no native input, sockets, or event loop is used.
class MemoryStream : public deskflow::IStream
{
public:
  std::vector<QByteArray> writes;

  void pushInput(const QByteArray &bytes)
  {
    m_input.append(bytes);
  }

  void close() override
  {
    m_input.clear();
  }

  uint32_t read(void *buffer, uint32_t count) override
  {
    const auto size = std::min(count, static_cast<uint32_t>(m_input.size()));
    if (buffer != nullptr && size != 0) {
      std::memcpy(buffer, m_input.constData(), size);
    }
    m_input.remove(0, size);
    return size;
  }

  void write(const void *buffer, uint32_t count) override
  {
    // Keep the overflow regression safe against the old implementation, which
    // forwards the deliberately invalid null payload to the underlying stream.
    if (buffer == nullptr) {
      writes.emplace_back();
    } else {
      writes.emplace_back(static_cast<const char *>(buffer), count);
    }
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
    close();
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<MemoryStream *>(this);
  }

  bool isReady() const override
  {
    return !m_input.isEmpty();
  }

  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_input.size());
  }

private:
  QByteArray m_input;
};

class ReadablePacketStream : public PacketStreamFilter
{
public:
  using PacketStreamFilter::PacketStreamFilter;

  void receive()
  {
    filterEvent(Event(EventTypes::StreamInputReady, getStream()->getEventTarget()));
  }
};

} // namespace

class PacketStreamFilterTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void writesCompletePacketOnce_data()
  {
    QTest::addColumn<QByteArray>("payload");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("mouse-ack") << QByteArray("CNOP") << QByteArray::fromHex("00000004") + "CNOP";
    QTest::newRow("mouse-move") << QByteArray("DMMV") + QByteArray::fromHex("0123 0456")
                              << QByteArray::fromHex("00000008") + "DMMV" + QByteArray::fromHex("0123 0456");

    const auto binary = QByteArray::fromHex("0044 00ff 7f00 0100");
    QTest::newRow("embedded-zeros") << binary << QByteArray::fromHex("00000008") + binary;

    const QByteArray stackBoundary(252, '\x7f');
    QTest::newRow("stack-boundary") << stackBoundary << QByteArray::fromHex("000000fc") + stackBoundary;
    const QByteArray heapBoundary(253, '\x80');
    QTest::newRow("heap-boundary") << heapBoundary << QByteArray::fromHex("000000fd") + heapBoundary;

    QByteArray clipboard("DCLP");
    for (uint32_t i = 0; i < 65536; ++i) {
      clipboard.append(static_cast<char>((i * 37) & 0xff));
    }
    QTest::newRow("large-clipboard") << clipboard << QByteArray::fromHex("00010004") + clipboard;
  }

  void writesCompletePacketOnce()
  {
    QFETCH(QByteArray, payload);
    QFETCH(QByteArray, expected);

    MockEventQueue events;
    MemoryStream stream;
    PacketStreamFilter filter(&events, &stream, false);
    filter.write(payload.constData(), static_cast<uint32_t>(payload.size()));

    // The receiver cannot use a length header alone. A single write also keeps
    // the socket thread from sending it before the corresponding payload.
    QCOMPARE(stream.writes.size(), size_t{1});
    QCOMPARE(stream.writes.front(), expected);
  }

  void consecutivePacketsPreserveFramingAndDecode()
  {
    MockEventQueue events;
    MemoryStream output;
    PacketStreamFilter writer(&events, &output, false);
    const QByteArray ack("CNOP");
    const auto move = QByteArray("DMMV") + QByteArray::fromHex("0123 0456");
    const auto binary = QByteArray::fromHex("00ff 0100");
    for (const auto &payload : {ack, move, binary}) {
      writer.write(payload.constData(), static_cast<uint32_t>(payload.size()));
    }

    const auto expected = QByteArray::fromHex("00000004") + ack + QByteArray::fromHex("00000008") + move +
                          QByteArray::fromHex("00000004") + binary;
    QByteArray wire;
    for (const auto &write : output.writes) {
      wire.append(write);
    }
    QCOMPARE(wire, expected);
    QCOMPARE(output.writes.size(), size_t{3});

    MemoryStream input;
    ReadablePacketStream reader(&events, &input, false);
    input.pushInput(wire.left(2));
    reader.receive();
    QVERIFY(!reader.isReady());
    input.pushInput(wire.mid(2, 5));
    reader.receive();
    QVERIFY(!reader.isReady());
    input.pushInput(wire.mid(7));
    reader.receive();

    char bytes[32];
    QCOMPARE(reader.read(bytes, sizeof(bytes)), uint32_t{4});
    QCOMPARE(QByteArray(bytes, 4), ack);
    QCOMPARE(reader.getSize(), uint32_t{8});
    QCOMPARE(reader.read(bytes, 3), uint32_t{3});
    QByteArray decodedMove(bytes, 3);
    QCOMPARE(reader.read(bytes, sizeof(bytes)), uint32_t{5});
    decodedMove.append(bytes, 5);
    QCOMPARE(decodedMove, move);
    QCOMPARE(reader.read(bytes, sizeof(bytes)), uint32_t{4});
    QCOMPARE(QByteArray(bytes, 4), binary);
    QVERIFY(!reader.isReady());
    QCOMPARE(reader.read(bytes, sizeof(bytes)), uint32_t{0});
  }

  void emptyPayloadKeepsLengthHeader()
  {
    MockEventQueue events;
    MemoryStream stream;
    PacketStreamFilter filter(&events, &stream, false);
    filter.write(nullptr, 0);

    QCOMPARE(stream.writes.size(), size_t{1});
    QCOMPARE(stream.writes.front(), QByteArray::fromHex("00000000"));
  }

  void rejectsFrameSizeOverflowBeforeWriting()
  {
    MockEventQueue events;
    MemoryStream stream;
    PacketStreamFilter filter(&events, &stream, false);

    QVERIFY_THROWS_EXCEPTION(IOException, filter.write(nullptr, std::numeric_limits<uint32_t>::max()));
    QVERIFY(stream.writes.empty());
  }
};

QTEST_GUILESS_MAIN(PacketStreamFilterTests)
#include "PacketStreamFilterTests.moc"
