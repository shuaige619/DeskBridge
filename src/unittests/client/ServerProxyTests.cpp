/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerProxyTests.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "client/Client.h"
#include "client/ServerProxy.h"
#include "common/Settings.h"
#include "common/SourceDeviceOptions.h"
#include "deskflow/Clipboard.h"
#include "deskflow/Computer.h"
#include "deskflow/PlatformComputer.h"
#include "deskflow/AppUtil.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "net/ISocketFactory.h"

#include <QTest>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

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

class FakeStream : public deskflow::IStream
{
public:
  void push(const std::string &bytes)
  {
    m_chunks.push_back(bytes);
  }

  void close() override
  {
    m_chunks.clear();
    m_inputShutdown = true;
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    if (m_inputShutdown || m_chunks.empty() || size == 0) {
      return 0;
    }

    if (beforeRead) {
      beforeRead(m_bytesRead);
    }
    auto &front = m_chunks.front();
    const size_t bytesToRead = std::min(static_cast<size_t>(size), front.size());
    if (buffer != nullptr) {
      std::memcpy(buffer, front.data(), bytesToRead);
    }

    m_bytesRead += bytesToRead;
    front.erase(0, bytesToRead);
    if (front.empty()) {
      m_chunks.pop_front();
    }
    return static_cast<uint32_t>(bytesToRead);
  }

  void write(const void *buffer, uint32_t size) override
  {
    m_written.append(static_cast<const char *>(buffer), size);
  }

  const std::string &written() const { return m_written; }
  void clearWritten() { m_written.clear(); }
  size_t bytesRead() const { return m_bytesRead; }
  std::function<void(size_t)> beforeRead;

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
    return const_cast<FakeStream *>(this);
  }

  bool isReady() const override
  {
    return !m_inputShutdown && !m_chunks.empty();
  }

  uint32_t getSize() const override
  {
    size_t total = 0;
    for (const auto &chunk : m_chunks) {
      total += chunk.size();
    }
    return static_cast<uint32_t>(std::min<size_t>(total, UINT32_MAX));
  }

private:
  std::deque<std::string> m_chunks;
  bool m_inputShutdown = false;
  std::string m_written;
  size_t m_bytesRead = 0;
};

class RecordingEventQueue : public IEventQueue
{
public:
  ~RecordingEventQueue() override
  {
    for (const auto &event : m_addedEvents) {
      Event::deleteData(event);
    }
  }

  int loop() override
  {
    return 0;
  }

  void adoptBuffer(IEventQueueBuffer *) override
  {
  }

  bool getEvent(Event &, double = -1.0) override
  {
    return false;
  }

  bool dispatchEvent(const Event &event) override
  {
    const auto handler = m_handlers.find(HandlerKey{event.getType(), event.getTarget()});
    if (handler == m_handlers.end()) {
      return false;
    }

    const auto callback = handler->second;
    callback(event);
    return true;
  }

  void addEvent(Event &&event) override
  {
    m_addedEvents.emplace_back(std::move(event));
  }

  EventQueueTimer *newTimer(double, void *) override
  {
    return timer();
  }

  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return timer();
  }

  void deleteTimer(EventQueueTimer *) override
  {
  }

  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  {
    m_handlers[HandlerKey{type, target}] = handler;
  }

  void removeHandler(EventTypes type, void *target) override
  {
    m_handlers.erase(HandlerKey{type, target});
  }

  void removeHandlers(void *target) override
  {
    for (auto it = m_handlers.begin(); it != m_handlers.end();) {
      if (it->first.target == target) {
        it = m_handlers.erase(it);
      } else {
        ++it;
      }
    }
  }

  void waitForReady() const override
  {
  }

  void *getSystemTarget() override
  {
    return this;
  }

  EventQueueTimer *timer()
  {
    return reinterpret_cast<EventQueueTimer *>(&m_timerStorage);
  }

  const std::vector<Event> &addedEvents() const
  {
    return m_addedEvents;
  }

private:
  struct HandlerKey
  {
    EventTypes type;
    void *target;

    bool operator<(const HandlerKey &other) const
    {
      if (type != other.type) {
        return static_cast<uint32_t>(type) < static_cast<uint32_t>(other.type);
      }
      return std::less<void *>{}(target, other.target);
    }
  };

  int m_timerStorage = 0;
  std::map<HandlerKey, EventHandler> m_handlers;
  std::vector<Event> m_addedEvents;
};

// The real Client needs a Computer for construction and cleanup. This inert
// platform never opens a display, installs input hooks or touches the clipboard.
class InertComputer : public PlatformComputer
{
public:
  explicit InertComputer(IEventQueue *events) : PlatformComputer(events) {}
  bool isPrimary() const override { return false; }
  void *getEventTarget() const override { return const_cast<InertComputer *>(this); }
  bool getClipboard(ClipboardID, IClipboard *) const override { return false; }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override { x = y = 0; w = h = 1000; }
  void getCursorPos(int32_t &x, int32_t &y) const override { x = y = 0; }
  void enable() override {}
  void disable() override {}
  void enter() override {}
  bool canLeave() override { return true; }
  void leave() override {}
  bool setClipboard(ClipboardID, const IClipboard *) override { return true; }
  void checkClipboards() override {}
  void openScreensaver(bool) override {}
  void closeScreensaver() override {}
  void screensaver(bool) override {}
  void resetOptions() override {}
  void setOptions(const OptionsList &) override {}
  void setSequenceNumber(uint32_t) override {}
  std::string getSecureInputApp() const override { return {}; }
  void reconfigure(uint32_t) override {}
  uint32_t activeSides() override { return 0; }
  void warpCursor(int32_t, int32_t) override {}
  uint32_t registerHotKey(KeyID, KeyModifierMask) override { return 0; }
  void unregisterHotKey(uint32_t) override {}
  void fakeInputBegin() override {}
  void fakeInputEnd() override {}
  int32_t getJumpZoneSize() const override { return 0; }
  bool isAnyMouseButtonDown(uint32_t &) const override { return false; }
  void getCursorCenter(int32_t &x, int32_t &y) const override { x = y = 500; }
  void fakeMouseButton(ButtonID, bool) override {}
  void fakeMouseMove(int32_t, int32_t) override {}
  void fakeMouseRelativeMove(int32_t, int32_t) const override {}
  void fakeMouseWheel(ScrollDelta) const override {}
protected:
  void updateButtons() override {}
  IKeyState *getKeyState() const override { return nullptr; }
  void handleSystemEvent(const Event &) override {}
};

class UnusedSocketFactory : public ISocketFactory
{
public:
  IDataSocket *create(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    qFatal("ServerProxy tests must not create a network socket");
    return nullptr;
  }
  IListenSocket *createListen(IArchNetwork::AddressFamily, SecurityLevel) const override
  {
    qFatal("ServerProxy tests must not create a listening socket");
    return nullptr;
  }
};

// Record only the proxy's Client boundary: all parsing, coalescing, handshake,
// acknowledgments and event dispatch still run the production ServerProxy code.
class RecordingClient : public Client
{
public:
  RecordingClient(IEventQueue *events, deskflow::Computer *computer)
      : Client(events, "test-client", NetworkAddress{}, new UnusedSocketFactory, computer) {}
  void handshakeComplete() override { handshaken = true; }
  void setOptions(const OptionsList &) override {}
  void mouseMove(int32_t x, int32_t y) override
  {
    cursorX = x;
    cursorY = y;
    calls << QString("move(%1,%2)").arg(x).arg(y);
  }
  void mouseRelativeMove(int32_t x, int32_t y) override
  {
    cursorX += x;
    cursorY += y;
    calls << QString("relative(%1,%2)").arg(x).arg(y);
  }
  void mouseDown(ButtonID) override { calls << "down"; }
  void mouseUp(ButtonID) override { calls << "up"; }
  void mouseWheel(int32_t, int32_t) override { calls << "wheel"; }
  void keyDown(KeyID, KeyModifierMask, KeyButton, const std::string &) override { calls << "keyDown"; }
  void keyUp(KeyID, KeyModifierMask, KeyButton) override { calls << "keyUp"; }
  void keyRepeat(KeyID, KeyModifierMask, int32_t, KeyButton, const std::string &) override { calls << "keyRepeat"; }
  bool leave() override { calls << "leave"; return true; }
  void enter(int32_t x, int32_t y, uint32_t, KeyModifierMask, bool) override
  {
    cursorX = x;
    cursorY = y;
    calls << QString("enter(%1,%2)").arg(x).arg(y);
  }
  void setClipboard(ClipboardID, const IClipboard *clipboard) override
  {
    clipboard->open(0);
    clipboardText = clipboard->get(IClipboard::Format::Text);
    clipboard->close();
    calls << "clipboard";
  }
  void grabClipboard(ClipboardID) override { calls << "grabClipboard"; }
  QStringList calls;
  std::string clipboardText;
  int32_t cursorX = 0;
  int32_t cursorY = 0;
  bool handshaken = false;
};

template <typename... Args> std::string message(const char *format, Args... args)
{
  FakeStream stream;
  ProtocolUtil::writef(&stream, format, args...);
  return stream.written();
}

std::string noopReplies(size_t count)
{
  std::string result;
  for (size_t i = 0; i < count; ++i) {
    result += kMsgCNoop;
  }
  return result;
}

struct InputFixture
{
  RecordingEventQueue events;
  deskflow::Computer computer{new InertComputer(&events), &events};
  RecordingClient client{&events, &computer};
  FakeStream stream;
  ServerProxy proxy{&client, &stream, &events};

  InputFixture()
  {
    const OptionsList options;
    receive(message(kMsgDSetOptions, &options));
    stream.clearWritten();
  }

  bool receive(const std::string &bytes)
  {
    stream.push(bytes);
    return events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget()));
  }
};

class TestServerProxy : public ServerProxy
{
public:
  using ServerProxy::ServerProxy;

  bool parseHandshakeMessageReturnsDisconnect(const uint8_t *code)
  {
    return parseHandshakeMessage(code) == ConnectionResult::Disconnect;
  }
};

Client *undereferenceableClient()
{
  // These paths must queue cleanup without calling through to Client.
  return reinterpret_cast<Client *>(0x1);
}

TestAppUtil &testAppUtil()
{
  static TestAppUtil util;
  return util;
}

const Client::DisconnectRequest *disconnectRequest(const RecordingEventQueue &events)
{
  if (events.addedEvents().size() != 1) {
    return nullptr;
  }
  return static_cast<const Client::DisconnectRequest *>(events.addedEvents().front().getDataObject());
}

} // namespace

void ServerProxyTests::initTestCase()
{
  (void)testAppUtil();
  QVERIFY(m_settingsDirectory.isValid());
  Settings::setSettingsFile(m_settingsDirectory.filePath("settings.conf"));
  Settings::setStateFile(m_settingsDirectory.filePath("state.conf"));
  m_log.setFilter(LogLevel::Level::Debug);
}

void ServerProxyTests::handleKeepAliveAlarm_timeout_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::Timer, events.timer())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server is not responding"));
}

void ServerProxyTests::handleData_incompleteMessage_queuesDisconnectRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  stream.push("DF");
  ServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(events.dispatchEvent(Event(EventTypes::StreamInputReady, stream.getEventTarget())));

  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto &event = events.addedEvents().front();
  QVERIFY(event.getType() == EventTypes::ClientDisconnectRequested);
  QCOMPARE(event.getTarget(), stream.getEventTarget());

  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Disconnect);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("incomplete message from server"));
}

void ServerProxyTests::parseHandshakeMessage_protocolError_queuesRefusalRequest()
{
  RecordingEventQueue events;
  FakeStream stream;
  TestServerProxy proxy(undereferenceableClient(), &stream, &events);

  QVERIFY(proxy.parseHandshakeMessageReturnsDisconnect(reinterpret_cast<const uint8_t *>(kMsgEBad)));
  QCOMPARE(events.addedEvents().size(), static_cast<size_t>(1));
  const auto *request = disconnectRequest(events);
  QVERIFY(request != nullptr);
  QVERIFY(request->kind() == Client::DisconnectRequest::Kind::Refuse);
  QVERIFY(request->refusalReason() == deskflow::core::ConnectionRefusal::ProtocolError);
  QCOMPARE(QString::fromUtf8(request->message()), QStringLiteral("server reported a protocol error"));
}

void ServerProxyTests::sourceOptions_wireRoundTrip()
{
  for (const auto os : {deskflow::shortcuts::DeviceOS::MacOS, deskflow::shortcuts::DeviceOS::Windows,
                        deskflow::shortcuts::DeviceOS::Linux}) {
    FakeStream stream;
    const OptionsList options{kOptionHeartbeat, 5000, kOptionDeskBridgeVersion, 1,
                              kOptionDeskBridgeSourceOS, static_cast<uint32_t>(os)};
    ProtocolUtil::writef(&stream, kMsgDSetOptions, &options);
    const auto bytes = stream.written();
    QVERIFY(bytes.starts_with("DSOP")); // Existing 1.x message, no appended packet or hello change.
    stream.push(bytes.substr(4));
    OptionsList decoded;
    QVERIFY(ProtocolUtil::readf(&stream, kMsgDSetOptions + 4, &decoded));
    QCOMPARE(decoded, options);
    QCOMPARE(deskflow::shortcuts::sourceOSFromOptions(decoded), os);
    QCOMPARE(stream.getSize(), 0u);
  }
}

void ServerProxyTests::mouseMoves_mixedModes_preserveWireOrder_data()
{
  QTest::addColumn<bool>("absoluteFirst");
  QTest::newRow("absolute-then-relative") << true;
  QTest::newRow("relative-then-absolute") << false;
}

void ServerProxyTests::mouseMoves_mixedModes_preserveWireOrder()
{
  QFETCH(bool, absoluteFirst);
  InputFixture f;
  QVERIFY(f.client.handshaken);
  const auto absolute = message(kMsgDMouseMove, 100, 200);
  const auto relative = message(kMsgDMouseRelMove, 5, -7);
  QVERIFY(f.receive(absoluteFirst ? absolute + relative : relative + absolute));
  const QStringList expected = absoluteFirst ? QStringList{"move(100,200)", "relative(5,-7)"}
                                           : QStringList{"relative(5,-7)", "move(100,200)"};
  QCOMPARE(f.client.calls, expected);
  QCOMPARE(f.client.cursorX, absoluteFirst ? 105 : 100);
  QCOMPARE(f.client.cursorY, absoluteFirst ? 193 : 200);
  QCOMPARE(f.stream.written(), noopReplies(2));
}

void ServerProxyTests::mouseMoves_sameMode_coalesce_data()
{
  QTest::addColumn<bool>("relative");
  QTest::newRow("absolute") << false;
  QTest::newRow("relative") << true;
}

void ServerProxyTests::mouseMoves_sameMode_coalesce()
{
  QFETCH(bool, relative);
  InputFixture f;
  const auto *format = relative ? kMsgDMouseRelMove : kMsgDMouseMove;
  QVERIFY(f.receive(message(format, 10, 20) + message(format, 5, -7) + message(format, -3, 2)));
  QCOMPARE(f.client.calls, relative ? QStringList{"relative(12,15)"} : QStringList{"move(-3,2)"});
  QCOMPARE(f.stream.written(), noopReplies(3));
}

void ServerProxyTests::mouseMoves_alternatingGroups_keepDeltasAndBoundaries()
{
  InputFixture f;
  QVERIFY(f.receive(
      message(kMsgDMouseRelMove, 5, 1) + message(kMsgDMouseRelMove, 7, 2) + message(kMsgDMouseMove, 100, 200) +
      message(kMsgDMouseMove, 150, 250) + message(kMsgDMouseRelMove, 3, -4) + message(kMsgDMouseRelMove, -2, 6) +
      message(kMsgDMouseWheel, 0, 120)
  ));
  QCOMPARE(f.client.calls, (QStringList{"relative(12,3)", "move(150,250)", "relative(1,2)", "wheel"}));
  QCOMPARE(f.client.cursorX, 151);
  QCOMPARE(f.client.cursorY, 252);
  QCOMPARE(f.stream.written(), noopReplies(7));
}

void ServerProxyTests::mouseMoves_inputBoundaries_flushBeforeAction_data()
{
  QTest::addColumn<QByteArray>("action");
  QTest::addColumn<QString>("recordedAction");
  QTest::newRow("mouse-down") << QByteArray::fromStdString(message(kMsgDMouseDown, 1)) << "down";
  QTest::newRow("mouse-up") << QByteArray::fromStdString(message(kMsgDMouseUp, 1)) << "up";
  QTest::newRow("wheel") << QByteArray::fromStdString(message(kMsgDMouseWheel, 0, 120)) << "wheel";
  QTest::newRow("key-down") << QByteArray::fromStdString(message(kMsgDKeyDown1_1, 'a', 0, 10)) << "keyDown";
  QTest::newRow("key-up") << QByteArray::fromStdString(message(kMsgDKeyUp, 'a', 0, 10)) << "keyUp";
  QTest::newRow("key-repeat") << QByteArray::fromStdString(message(kMsgDKeyRepeat1_1, 'a', 0, 2, 10)) << "keyRepeat";
  QTest::newRow("leave") << QByteArray::fromStdString(message(kMsgCLeave)) << "leave";
}

void ServerProxyTests::mouseMoves_inputBoundaries_flushBeforeAction()
{
  QFETCH(QByteArray, action);
  QFETCH(QString, recordedAction);
  InputFixture f;
  QVERIFY(f.receive(message(kMsgDMouseMove, 100, 200) + action.toStdString() + message(kMsgDMouseMove, 300, 400)));
  QCOMPARE(f.client.calls, (QStringList{"move(100,200)", recordedAction, "move(300,400)"}));
  QCOMPARE(f.stream.written(), noopReplies(3));
}

void ServerProxyTests::mouseMoves_noop_keepsCoalescingAndReplies()
{
  InputFixture f;
  QVERIFY(f.receive(message(kMsgDMouseMove, 100, 200) + message(kMsgCNoop) + message(kMsgDMouseMove, 300, 400)));
  QCOMPARE(f.client.calls, (QStringList{"move(300,400)"}));
  QCOMPARE(f.stream.written(), noopReplies(3));
}

void ServerProxyTests::mouseMoves_infoChange_ignoresUntilAcknowledged()
{
  InputFixture f;
  f.proxy.onInfoChanged();
  f.stream.clearWritten(); // Ignore the outgoing display-info update for this assertion.
  QVERIFY(f.receive(message(kMsgDMouseMove, 100, 200) + message(kMsgDMouseRelMove, 5, -7)));
  QVERIFY(f.client.calls.isEmpty());
  QVERIFY(f.receive(message(kMsgCInfoAck) + message(kMsgDMouseMove, 300, 400)));
  QCOMPARE(f.client.calls, (QStringList{"move(300,400)"}));
  QCOMPARE(f.stream.written(), noopReplies(4));
}

void ServerProxyTests::mouseMoves_enter_discardsPreviousPosition()
{
  InputFixture f;
  QVERIFY(f.receive(message(kMsgDMouseRelMove, 5, -7) + message(kMsgCEnter, 100, 200, 1, 0)));
  QCOMPARE(f.client.calls, (QStringList{"enter(100,200)"}));
  QCOMPARE(f.stream.written(), noopReplies(2));
}

void ServerProxyTests::clipboard_flushesMouseBeforeReadingPayload_data()
{
  QTest::addColumn<int>("stage");
  QTest::newRow("start") << 0;
  QTest::newRow("data") << 1;
  QTest::newRow("finish") << 2;
  QTest::newRow("grab") << 3;
}

void ServerProxyTests::clipboard_flushesMouseBeforeReadingPayload()
{
  QFETCH(int, stage);
  InputFixture f;
  Clipboard clipboard;
  QVERIFY(clipboard.open(1));
  clipboard.add(IClipboard::Format::Text, "clipboard payload");
  clipboard.close();
  const auto data = clipboard.marshall();
  const auto size = std::to_string(data.size());
  const auto start = message(kMsgDClipboard, kClipboardClipboard, 1, ChunkType::DataStart, &size);
  const auto chunk = message(kMsgDClipboard, kClipboardClipboard, 1, ChunkType::DataChunk, &data);
  const std::string empty;
  const auto finish = message(kMsgDClipboard, kClipboardClipboard, 1, ChunkType::DataEnd, &empty);
  if (stage == 1 || stage == 2) {
    QVERIFY(f.receive(start + (stage == 2 ? chunk : std::string{})));
    f.stream.clearWritten();
  }
  const auto mouse = message(kMsgDMouseMove, 100, 200);
  const auto clipboardMessage = stage == 0 ? start : stage == 1 ? chunk : stage == 2 ? finish
                                                            : message(kMsgCClipboard, kClipboardClipboard, 1);
  const auto payloadOffset = f.stream.bytesRead() + mouse.size() + 4;
  bool observedPayloadRead = false;
  bool mouseAppliedBeforePayloadRead = false;
  f.stream.beforeRead = [&](size_t bytesRead) {
    if (bytesRead == payloadOffset) {
      observedPayloadRead = true;
      mouseAppliedBeforePayloadRead = f.client.calls == QStringList{"move(100,200)"};
    }
  };
  QVERIFY(f.receive(mouse + clipboardMessage));
  QVERIFY(observedPayloadRead);
  QVERIFY(mouseAppliedBeforePayloadRead);
  const auto expected = stage == 2 ? QStringList{"move(100,200)", "clipboard"}
                                   : stage == 3 ? QStringList{"move(100,200)", "grabClipboard"}
                                                : QStringList{"move(100,200)"};
  QCOMPARE(f.client.calls, expected);
  if (stage == 2) {
    QCOMPARE(f.client.clipboardText, std::string("clipboard payload"));
  }
  QCOMPARE(f.stream.written(), noopReplies(2));
}

QTEST_MAIN(ServerProxyTests)

void ServerProxyTests::screenLayout_capabilityGuardsWireCommands()
{
  InputFixture fixture;
  for (const OptionsList options : {OptionsList{}, OptionsList{kOptionDeskBridgeLayout, 3},
                                    OptionsList{kOptionDeskBridgeLayout, 1, kOptionDeskBridgeLayout, 1}}) {
    fixture.stream.clearWritten();
    QVERIFY(fixture.receive(message(kMsgDSetOptions, &options)));
    QCOMPARE(fixture.stream.written(), noopReplies(1));
  }
  fixture.stream.clearWritten();
  const OptionsList supported{kOptionDeskBridgeLayout, 1};
  QVERIFY(fixture.receive(message(kMsgDSetOptions, &supported)));
#if DESKBRIDGE_PERSONAL_BUILD
  QCOMPARE(fixture.stream.written(), message(kMsgQScreenLayout, 0, 0, 0) + noopReplies(1));
#else
  QCOMPARE(fixture.stream.written(), noopReplies(1));
#endif
}

void ServerProxyTests::screenLayout_acceptsNegotiatedSnapshot()
{
#if DESKBRIDGE_PERSONAL_BUILD
  InputFixture fixture;
  const OptionsList supported{kOptionDeskBridgeLayout, 1};
  QVERIFY(fixture.receive(message(kMsgDSetOptions, &supported)));
  fixture.stream.clearWritten();
  const std::string server = "source";
  const std::string client = "test-client";
  const std::string error;
  QVERIFY(fixture.receive(message(kMsgDScreenLayout, 3, 1, 0, 1, &server, &client, &error)));
  QVERIFY(fixture.events.addedEvents().empty());
  QCOMPARE(fixture.stream.written(), noopReplies(1));
#endif
}

void ServerProxyTests::screenLayout_rejectsOversizedAndWrongIdentity()
{
#if DESKBRIDGE_PERSONAL_BUILD
  for (const auto malformed : {0, 1, 2, 3}) {
    InputFixture fixture;
    const OptionsList supported{kOptionDeskBridgeLayout, 1};
    QVERIFY(fixture.receive(message(kMsgDSetOptions, &supported)));
    const std::string server = "source";
    const std::string client = malformed == 1 ? "another-client" : "test-client";
    const std::string error;
    std::string payload;
    if (malformed == 0) {
      // A declared oversized string must fail before reading/allocating it.
      payload = message("DLAY%4i%4i%4i%4i%4i", 1, 1, 0, 1, 0xffffffffu);
    } else {
      payload = message(kMsgDScreenLayout, malformed == 2 ? 88 : 1, malformed == 3 ? 2 : 1, 0, 1,
                        &server, &client, &error);
    }
    QVERIFY(fixture.receive(payload));
    const auto *request = disconnectRequest(fixture.events);
    QVERIFY(request);
    QCOMPARE(request->kind(), Client::DisconnectRequest::Kind::Disconnect);
  }
#endif
}

void ServerProxyTests::screenLayout_validatesMetadataAndRequests()
{
  using namespace deskflow::layout;
  QVERIFY(!hasCapability({}));
  QVERIFY(!hasCapability({kOptionDeskBridgeLayout, 0}));
  QVERIFY(!hasCapability({kOptionDeskBridgeLayout, 1, kOptionDeskBridgeLayout, 1}));
  QVERIFY(!hasCapability({kOptionDeskBridgeLayout}));
  QVERIFY(hasCapability({kOptionClipboardSharing, 1, kOptionDeskBridgeLayout, 1}));
  QVERIFY(validRequest(0, 0, 0));
  QVERIFY(!validRequest(0, 1, 2));
  QVERIFY(!validRequest(1, 0, 2));
  QVERIFY(!validRequest(1, 2, 0));
  QVERIFY(!validRequest(1, 99, 2));
  QVERIFY(validRequest(0xffffffffu, 4, 1));
  const Snapshot snapshot{QStringLiteral("source"), QStringLiteral("client"), deskflow::layout::Direction::Above, true,
                           QStringLiteral("a=b\nstop")};
  const auto payload = encodeIpc(toJson(snapshot));
  QVERIFY(!payload.contains(u'='));
  QVERIFY(!payload.contains(u'\n'));
  const auto decoded = decodeIpc(payload);
  QVERIFY(decoded);
  QCOMPARE(snapshotFromJson(*decoded), std::optional<Snapshot>(snapshot));
  QVERIFY(!decodeIpc(QString(kMaxIpcPayloadLength + 2, u'a')));
  QVERIFY(!decodeIpc(QStringLiteral("gg")));
  QVERIFY(!decodeIpc(QStringLiteral("f")));
  auto malformed = toJson(snapshot);
  malformed.insert("direction", 1.5);
  QVERIFY(!snapshotFromJson(malformed));
  malformed = toJson(snapshot);
  malformed.insert("server", QString(kMaxNameLength + 1, u'a'));
  QVERIFY(!snapshotFromJson(malformed));
  malformed = toJson(snapshot);
  malformed.insert("editable", 1);
  QVERIFY(!snapshotFromJson(malformed));
  QVERIFY(!requestIdFromJson({{"id", 0}}));
  QVERIFY(!requestIdFromJson({{"id", 1.5}}));
  QVERIFY(!requestIdFromJson({{"id", 4294967296.0}}));
  QCOMPARE(requestIdFromJson({{"id", 4294967295.0}}), std::optional<quint32>(0xffffffffu));
}

void ServerProxyTests::screenLayout_v2TopologyRoundTripAndBounds()
{
#if DESKBRIDGE_PERSONAL_BUILD
  using namespace deskflow::layout;
  InputFixture fixture;
  const OptionsList supported{kOptionDeskBridgeLayout, 2};
  fixture.stream.clearWritten();
  QVERIFY(fixture.receive(message(kMsgDSetOptions, &supported)));
  FakeStream query;
  writeWireObject(&query, kMsgQScreenLayoutV2, {{"kind", QStringLiteral("query")}, {"id", 0}});
  QCOMPARE(fixture.stream.written(), query.written() + noopReplies(1));
  Snapshot layout{"source", {}, deskflow::layout::Direction::Unknown, true, {}};
  layout.protocolVersion = 2;
  layout.screens = {{"source", 0, 0}, {"canonical-client", 1, 0}, {"other", 1, 1}, {"fourth", 0, 1}};
  layout.localName = "canonical-client"; // Accepted connection may use an alias.
  layout.connectedNames = {"source", "canonical-client", "other"};
  layout.revision = revisionFor(layout);
  FakeStream response;
  writeWireObject(&response, kMsgDScreenLayoutV2, {{"id", 0}, {"success", true}, {"snapshot", toJson(layout)}});
  fixture.stream.clearWritten();
  QVERIFY(fixture.receive(response.written()));
  QVERIFY(fixture.events.addedEvents().empty());
  QCOMPARE(fixture.stream.written(), noopReplies(1));
  // Bound declared JSON length before allocating or attempting to read it.
  QVERIFY(fixture.receive(message("DLY2%4i", kMaxWirePayloadLength + 1)));
  const auto *request = disconnectRequest(fixture.events);
  QVERIFY(request);
  QCOMPARE(request->kind(), Client::DisconnectRequest::Kind::Disconnect);
#endif
}
