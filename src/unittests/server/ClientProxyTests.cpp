/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClientProxyTests.h"

#include "../deskflow/MockEventQueue.h"
#include "deskflow/AppUtil.h"
#include "deskflow/ScreenLayoutProtocol.h"
#include "io/IStream.h"
#include "server/ClientProxy1_0.h"
#include "server/ClientProxy1_1.h"
#include "server/ClientProxy1_6.h"
#include "server/ClientProxy1_7.h"
#include "server/ClientProxy1_8.h"

#include <memory>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

#include <QByteArray>
#include <QTest>

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {"en"};
  }

  std::string getCurrentLanguageCode() override
  {
    return "en";
  }
};

class CapturingStream : public deskflow::IStream
{
public:
  QByteArray take()
  {
    auto bytes = m_buffer;
    m_buffer.clear();
    return bytes;
  }

  void write(const void *buffer, uint32_t n) override
  {
    m_buffer.append(static_cast<const char *>(buffer), n);
  }

  void close() override
  {
  }

  void push(const QByteArray &bytes) { m_input.append(bytes); }
  uint32_t read(void *destination, uint32_t requested) override
  {
    const auto size = std::min<qsizetype>(m_input.size(), requested);
    if (destination && size) std::memcpy(destination, m_input.constData(), size);
    m_input.remove(0, size);
    return static_cast<uint32_t>(size);
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<CapturingStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }

private:
  QByteArray m_buffer;
  QByteArray m_input;
};

class LayoutParsingProxy : public ClientProxy1_0
{
public:
  using ClientProxy1_0::ClientProxy1_0;
  bool parseCode(const char *code) { return parseMessage(reinterpret_cast<const uint8_t *>(code)); }
};

class LayoutRequestQueue : public MockEventQueue
{
public:
  ~LayoutRequestQueue() override { for (const auto &event : events) Event::deleteData(event); }
  void addEvent(Event &&event) override { events.push_back(std::move(event)); }
  std::vector<Event> events;
};

std::unique_ptr<ClientProxy> makeProxy(int minor, deskflow::IStream *stream, IEventQueue *events)
{
  // the 1.4 and later constructors assert the server pointer is non-null but only store it
  auto *server = reinterpret_cast<Server *>(0x1);

  std::unique_ptr<ClientProxy> proxy;
  switch (minor) {
  case 0:
    proxy = std::make_unique<ClientProxy1_0>("client", stream, events);
    break;
  case 1:
    proxy = std::make_unique<ClientProxy1_1>("client", stream, events);
    break;
  case 6:
    proxy = std::make_unique<ClientProxy1_6>("client", stream, server, events);
    break;
  case 7:
    proxy = std::make_unique<ClientProxy1_7>("client", stream, server, events);
    break;
  case 8:
    proxy = std::make_unique<ClientProxy1_8>("client", stream, server, events);
    break;
  default:
    break;
  }
  return proxy;
}

struct ProxyUnderTest
{
  MockEventQueue events;
  CapturingStream *stream = new CapturingStream;
  std::unique_ptr<ClientProxy> proxy;

  explicit ProxyUnderTest(int minor) : proxy(makeProxy(minor, stream, &events))
  {
    // drop the query-info and layout-sync messages the constructors send
    stream->take();
  }
};

const KeyID kKey = 0x61;
const KeyModifierMask kMask = 0;
const KeyButton kButton = 0x1e;
const int32_t kCount = 3;
const std::string kLang = "en";

} // namespace

void ClientProxyTests::initTestCase()
{
  // the 1.8 constructor reads the keyboard layouts through AppUtil::instance()
  static TestAppUtil appUtil;
}

// These formats are frozen because shipped third-party clients parse them byte
// for byte: Synergy 1.4 through 1.14.1 negotiate 1.4 through 1.7, Barrier and
// Input Leap negotiate 1.6, and Synergy 1.14.2 onwards and Deskflow negotiate 1.8.
void ClientProxyTests::keyDown_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKDN" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKDN" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKDL" + QByteArray::fromHex("0061 0000 001e 00000002") + "en";
}

void ClientProxyTests::keyDown()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyDown(kKey, kMask, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyRepeat_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKRP" + QByteArray::fromHex("0061 0000 0003");
  QTest::newRow("1.1") << 1 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.6") << 6 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.7") << 7 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e");
  QTest::newRow("1.8") << 8 << "DKRP" + QByteArray::fromHex("0061 0000 0003 001e 00000002") + "en";
}

void ClientProxyTests::keyRepeat()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyRepeat(kKey, kMask, kCount, kButton, kLang);
  QCOMPARE(test.stream->take(), expected);
}

void ClientProxyTests::keyUp_data()
{
  QTest::addColumn<int>("minor");
  QTest::addColumn<QByteArray>("expected");

  QTest::newRow("1.0") << 0 << "DKUP" + QByteArray::fromHex("0061 0000");
  QTest::newRow("1.1") << 1 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.6") << 6 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.7") << 7 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
  QTest::newRow("1.8") << 8 << "DKUP" + QByteArray::fromHex("0061 0000 001e");
}

void ClientProxyTests::keyUp()
{
  QFETCH(int, minor);
  QFETCH(QByteArray, expected);

  ProxyUnderTest test(minor);
  test.proxy->keyUp(kKey, kMask, kButton);
  QCOMPARE(test.stream->take(), expected);
}

QTEST_MAIN(ClientProxyTests)

void ClientProxyTests::screenLayout_requiresNegotiationAndUsesConnectedAlias()
{
  ProxyUnderTest test(0);
  const deskflow::layout::Snapshot snapshot{"source", "canonical-client", deskflow::layout::Direction::Above, true, {}};
  test.proxy->sendScreenLayout(snapshot, 23, true);
  QVERIFY(test.stream->take().isEmpty());
  test.proxy->setLayoutAdvertised(true);
  test.proxy->sendScreenLayout(snapshot, 23, true);
  QVERIFY(test.stream->take().isEmpty());
  test.proxy->setLayoutSubscribed(true);
  test.proxy->sendScreenLayout(snapshot, 23, true);
  const auto expected = QByteArray("DLAY") + QByteArray::fromHex("00000003 00000001 00000017 00000001 00000006") +
                        "source" + QByteArray::fromHex("00000006") + "client" + QByteArray::fromHex("00000000");
  QCOMPARE(test.stream->take(), expected);
  test.proxy->setLayoutSubscribed(false);
  test.proxy->sendScreenLayout(snapshot);
  QVERIFY(test.stream->take().isEmpty());
}

void ClientProxyTests::screenTopology_threePeersReceiveSameGraphWithOwnIdentity()
{
  using namespace deskflow::layout;
  Snapshot topology{"source", {}, deskflow::layout::Direction::Unknown, true, {}};
  topology.screens = {{"source", 0, 0}, {"one", 1, 0}, {"two", 1, 1}, {"three", 0, 1}};
  topology.protocolVersion = 2;
  topology.connectedNames = {"source", "one", "two", "three"};
  topology.revision = revisionFor(topology);
  for (const auto *name : {"one", "two", "three"}) {
    MockEventQueue events;
    auto *stream = new CapturingStream;
    LayoutParsingProxy proxy(name, stream, &events);
    stream->take();
    proxy.setLayoutVersion(2);
    proxy.sendScreenLayout(topology);
    QVERIFY(stream->take().isEmpty()); // No custom frames before subscription.
    proxy.setLayoutSubscribed(true);
    topology.localName = name;
    proxy.sendScreenLayout(topology);
    const auto bytes = stream->take();
    QCOMPARE(bytes.left(4), QByteArray("DLY2"));
    const auto object = QJsonDocument::fromJson(bytes.mid(8)).object();
    const auto decoded = snapshotFromJson(object.value("snapshot").toObject());
    QVERIFY(decoded);
    QCOMPARE(decoded->screens, topology.screens);
    QCOMPARE(decoded->localName, QString::fromLatin1(name));
    QCOMPARE(decoded->connectedNames, topology.connectedNames);
    QCOMPARE(decoded->revision, topology.revision);
  }
}

void ClientProxyTests::screenTopology_requestsAreNegotiatedAndBounded()
{
  using namespace deskflow::layout;
  LayoutRequestQueue events;
  auto *stream = new CapturingStream;
  LayoutParsingProxy proxy("one", stream, &events);
  stream->take();
  const Move move{"three", -1, 0, QString(64, u'a')};
  CapturingStream outgoing;
  writeWireObject(&outgoing, kMsgQScreenLayoutV2,
      {{"kind", QStringLiteral("move")}, {"id", 123}, {"move", moveToJson(move)}});
  stream->push(outgoing.take().mid(4));
  proxy.setLayoutVersion(1);
  QVERIFY(!proxy.parseCode(kMsgQScreenLayoutV2));
  QVERIFY(events.events.empty());
  proxy.setLayoutVersion(2);
  QVERIFY(proxy.parseCode(kMsgQScreenLayoutV2));
  QCOMPARE(events.events.size(), size_t(1));
  const auto *command = dynamic_cast<const Command *>(events.events.front().getDataObject());
  QVERIFY(command);
  QCOMPARE(command->kind, Command::Kind::Move);
  QCOMPARE(command->id, quint32(123));
  QCOMPARE(command->move, move);
  QVERIFY(proxy.layoutSubscribed());
  stream->push(QByteArray::fromHex("00018001"));
  QVERIFY_EXCEPTION_THROWN(proxy.parseCode(kMsgQScreenLayoutV2), BadClientException);
  writeWireObject(&outgoing, kMsgQScreenLayoutV2,
      {{"kind", QStringLiteral("move")}, {"id", 0}, {"move", moveToJson(move)}});
  stream->push(outgoing.take().mid(4));
  QVERIFY_EXCEPTION_THROWN(proxy.parseCode(kMsgQScreenLayoutV2), BadClientException);
  QCOMPARE(events.events.size(), size_t(1));
}
