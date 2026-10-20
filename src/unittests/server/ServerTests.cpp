/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/Server.h"
#include "server/PrimaryClient.h"
#include "server/ClientProxy1_0.h"
#include "io/IStream.h"
#include <QtEndian>
#include "deskflow/ipc/CoreIpcServer.h"
#include "common/Settings.h"
#include "../deskflow/MockEventQueue.h"
#include <fstream>
#include <map>
#include <memory>
#include <vector>

namespace {
#if DESKBRIDGE_PERSONAL_BUILD
constexpr bool kPersonalLayoutEnabled = true;
#else
constexpr bool kPersonalLayoutEnabled = false;
#endif
class LayoutEventQueue : public MockEventQueue
{
public:
  void addHandler(EventTypes type, void *target, const EventHandler &handler) override
  { handlers[{type, target}] = handler; }
  void removeHandler(EventTypes type, void *target) override { handlers.erase({type, target}); }
  bool dispatchEvent(const Event &event) override
  {
    const auto found = handlers.find({event.getType(), event.getTarget()});
    if (found == handlers.end()) return false;
    auto callback = found->second;
    callback(event);
    return true;
  }
  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    timers.push_back(std::make_unique<int>(0));
    return reinterpret_cast<EventQueueTimer *>(timers.back().get());
  }
  std::map<std::pair<EventTypes, void *>, EventHandler> handlers;
  std::vector<std::unique_ptr<int>> timers;
};

// Every operation Server performs during a layout transaction is intercepted;
// the sentinel Computer is never dereferenced and no display/input is opened.
class LayoutPrimary : public PrimaryClient
{
public:
  LayoutPrimary() : PrimaryClient("source", reinterpret_cast<deskflow::Computer *>(1)) {}
  void *getEventTarget() const override { return const_cast<LayoutPrimary *>(this); }
  void enable() override {}
  void disable() override {}
  void getCursorPos(int32_t &x, int32_t &y) const override { x = y = 0; }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override { x = y = 0; w = h = 1000; }
  KeyModifierMask getToggleMask() const override { return 0; }
  void reconfigure(uint32_t value) override { sides = value; }
  void resetOptions() override {}
  void setOptions(const OptionsList &value) override { options = value; }
  uint32_t registerHotKey(KeyID, KeyModifierMask) override { return 1; }
  void unregisterHotKey(uint32_t) override {}
  uint32_t sides = 0;
  OptionsList options;
};

struct LayoutStreamState
{
  QByteArray written;
  bool closed = false;
  int writesAfterClose = 0;
};
class LayoutStream : public deskflow::IStream
{
public:
  explicit LayoutStream(std::shared_ptr<LayoutStreamState> state) : state(std::move(state)) {}
  void close() override { state->closed = true; }
  uint32_t read(void *, uint32_t) override { return 0; }
  void write(const void *bytes, uint32_t size) override
  {
    if (state->closed) ++state->writesAfterClose;
    state->written.append(static_cast<const char *>(bytes), size);
  }
  void flush() override {}
  void shutdownInput() override {}
  void shutdownOutput() override {}
  void *getEventTarget() const override { return const_cast<LayoutStream *>(this); }
  bool isReady() const override { return false; }
  uint32_t getSize() const override { return 0; }
  std::shared_ptr<LayoutStreamState> state;
};
class LayoutRemoteClient : public ClientProxy1_0
{
public:
  using ClientProxy1_0::ClientProxy1_0;
  void getCursorPos(int32_t &x, int32_t &y) const override { x = y = 0; }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override { x = y = 0; w = h = 1000; }
};
QList<QJsonObject> layoutFrames(const QByteArray &bytes)
{
  QList<QJsonObject> result;
  qsizetype position = 0;
  while ((position = bytes.indexOf("DLY2", position)) >= 0 && bytes.size() - position >= 8) {
    const auto size = qFromBigEndian<quint32>(bytes.constData() + position + 4);
    if (size > deskflow::layout::kMaxWirePayloadLength || bytes.size() - position - 8 < size) break;
    result.push_back(QJsonDocument::fromJson(bytes.mid(position + 8, size)).object());
    position += 8 + size;
  }
  return result;
}

struct LayoutServerFixture
{
  deskflow::core::ipc::CoreIpcServer ipc{nullptr};
  LayoutEventQueue events;
  deskflow::server::Config config{&events};
  LayoutPrimary primary;
  std::unique_ptr<Server> server;
  LayoutServerFixture()
  {
    Settings::removeUnknownComputers({"computer_source", "computer_receiver"});
    config.addComputer("source");
    config.addComputer("receiver");
    config.connect("source", Direction::Right, 0, 1, "receiver", 0, 1);
    config.connect("receiver", Direction::Left, 0, 1, "source", 0, 1);
    config.addOption("", kOptionDisableLockToComputer, 1);
    server = std::make_unique<Server>(config, &primary, reinterpret_cast<deskflow::Computer *>(1), &events);
  }
  bool command(const deskflow::layout::Command &command)
  {
    return events.dispatchEvent(Event(EventTypes::ScreenLayoutCommand, server.get(),
                                      const_cast<deskflow::layout::Command *>(&command)));
  }
  void controller(bool available)
  {
    deskflow::layout::Command query;
    query.controllerAvailable = available;
    command(query);
  }
  void request(quint32 id, deskflow::layout::Direction expected, deskflow::layout::Direction desired)
  {
    deskflow::layout::Command request;
    request.kind = deskflow::layout::Command::Kind::Request;
    request.id = id;
    request.expected = expected;
    request.desired = desired;
    command(request);
  }
  void complete(quint32 transaction, bool success)
  {
    deskflow::layout::Command complete;
    complete.kind = deskflow::layout::Command::Kind::Complete;
    complete.id = transaction;
    complete.success = success;
    command(complete);
  }
  void move(quint32 id, const deskflow::layout::Move &move)
  {
    deskflow::layout::Command request;
    request.kind = deskflow::layout::Command::Kind::Move;
    request.id = id;
    request.move = move;
    command(request);
  }
  void topology(const QList<deskflow::layout::Screen> &screens)
  {
    for (const auto &screen : screens) {
      if (!config.isComputer(screen.name.toStdString())) config.addComputer(screen.name.toStdString());
      Settings::setValue(Settings::Computer::Name.arg(screen.name), screen.name);
    }
    routes(config, screens);
    server->setConfig(config);
  }
  static void routes(deskflow::server::Config &target, const QList<deskflow::layout::Screen> &screens)
  {
    for (const auto &screen : screens)
      for (const auto side : {Direction::Left, Direction::Right, Direction::Top, Direction::Bottom})
        target.disconnect(screen.name.toStdString(), side);
    for (const auto &a : screens) for (const auto &b : screens) {
      Direction side = Direction::NoDirection;
      if (b.column == a.column - 1 && b.row == a.row) side = Direction::Left;
      if (b.column == a.column + 1 && b.row == a.row) side = Direction::Right;
      if (b.row == a.row - 1 && b.column == a.column) side = Direction::Top;
      if (b.row == a.row + 1 && b.column == a.column) side = Direction::Bottom;
      if (side != Direction::NoDirection) target.connect(a.name.toStdString(), side, 0, 1, b.name.toStdString(), 0, 1);
    }
  }
  bool saveTopology(const deskflow::layout::Snapshot &snapshot)
  {
    deskflow::server::Config candidate(&events);
    candidate = config;
    routes(candidate, snapshot.screens);
    std::ofstream stream(Settings::serverConfigFile().toStdString());
    stream << candidate;
    return stream.good();
  }
  bool save(deskflow::layout::Direction desired)
  {
    deskflow::server::Config candidate(&events);
    candidate = config;
    for (const auto side : {Direction::Left, Direction::Right, Direction::Top, Direction::Bottom}) {
      candidate.disconnect("source", side);
      candidate.disconnect("receiver", side);
    }
    candidate.connect("source", static_cast<Direction>(desired), 0, 1, "receiver", 0, 1);
    candidate.connect("receiver", static_cast<Direction>(deskflow::layout::opposite(desired)), 0, 1, "source", 0, 1);
    std::ofstream stream(Settings::serverConfigFile().toStdString());
    stream << candidate;
    return stream.good();
  }
};
}

void ServerTests::SwitchToComputerInfo_alloc_computer()
{
  auto actual = new Server::SwitchToComputerInfo("test");
  QCOMPARE(actual->m_computer, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndComputers()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_computers, "test");
  delete info;
}

QTEST_MAIN(ServerTests)

void ServerTests::initTestCase()
{
  m_arch.init();
  QVERIFY(m_files.isValid());
  Settings::setSettingsFile(m_files.filePath("settings.conf"));
  Settings::setStateFile(m_files.filePath("state.conf"));
  Settings::setValue(Settings::Server::ExternalConfig, false);
  Settings::setValue(Settings::Computer::Name.arg("source"), "source");
  Settings::setValue(Settings::Computer::Name.arg("receiver"), "receiver");
  Settings::setValue(Settings::Server::DisableLockToComputer, true);
}

void ServerTests::screenLayout_appliesOnlyAfterMatchingPersistedCompletion()
{
  using D = deskflow::layout::Direction;
  LayoutServerFixture fixture;
  fixture.controller(true);
  const auto keyTarget = fixture.primary.getEventTarget();
  QVERIFY(fixture.events.handlers.contains({EventTypes::KeyStateKeyDown, keyTarget}));
  fixture.request(77, D::Right, D::Above);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  QVERIFY(fixture.save(D::Above));
  fixture.complete(999, true); // A stale GUI completion has no effect.
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Above);
  QVERIFY(fixture.events.handlers.contains({EventTypes::KeyStateKeyDown, keyTarget}));
  QCOMPARE(fixture.config.getNeighbor("source", Direction::Top, 0.5, nullptr), std::string("receiver"));
  QCOMPARE(fixture.config.getNeighbor("receiver", Direction::Bottom, 0.5, nullptr), std::string("source"));
}

void ServerTests::screenLayout_rejectsStaleMalformedAndUnavailableRequests()
{
  using D = deskflow::layout::Direction;
  LayoutServerFixture fixture;
  fixture.request(1, D::Right, D::Above); // No GUI/controller.
  fixture.complete(1, true);
  fixture.controller(true);
  fixture.request(2, D::Left, D::Above); // Stale expected direction.
  fixture.request(0, D::Right, D::Above); // Reserved subscription ID.
  fixture.request(3, D::Right, D::Unknown);
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  fixture.request(4, D::Right, D::Below);
  fixture.request(5, D::Right, D::Above); // One pending transaction.
  QVERIFY(fixture.save(D::Below));
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Below);
}

void ServerTests::screenLayout_timeoutAndControllerLossKeepRuntimeUnchanged()
{
  using D = deskflow::layout::Direction;
  LayoutServerFixture fixture;
  fixture.controller(true);
  fixture.request(1, D::Right, D::Above);
  QVERIFY(fixture.save(D::Above));
  QVERIFY(!fixture.events.timers.empty());
  auto *timer = reinterpret_cast<EventQueueTimer *>(fixture.events.timers.back().get());
  QVERIFY(fixture.events.dispatchEvent(Event(EventTypes::Timer, timer)));
  fixture.complete(1, true); // A late save cannot resurrect the edit.
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  fixture.request(2, D::Right, D::Below);
  fixture.controller(false);
  QVERIFY(fixture.save(D::Below));
  fixture.complete(2, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
}

void ServerTests::screenLayout_invalidSavedConfigKeepsRoutingAndKeyHandlers()
{
  using D = deskflow::layout::Direction;
  LayoutServerFixture fixture;
  fixture.controller(true);
  fixture.request(1, D::Right, D::Above);
  QVERIFY(fixture.save(D::Below)); // Does not match the pending request.
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  QVERIFY(fixture.events.handlers.contains({EventTypes::KeyStateKeyDown, fixture.primary.getEventTarget()}));
  fixture.request(2, D::Right, D::Above);
  { std::ofstream file(Settings::serverConfigFile().toStdString()); file << "this is not a configuration"; }
  fixture.complete(2, true);
  QCOMPARE(fixture.config.screenLayout("source").direction, D::Right);
  QVERIFY(fixture.events.handlers.contains({EventTypes::KeyStateKeyDown, fixture.primary.getEventTarget()}));
}

void ServerTests::screenTopology_movesThreeAndFourNodeGraphs()
{
  using namespace deskflow::layout;
  for (const int nodes : {3, 4}) {
    LayoutServerFixture fixture;
    QList<Screen> screens{{"source", 0, 0}, {"receiver", 1, 0}, {"third", 1, 1}};
    if (nodes == 4) screens.push_back({"fourth", 0, 1});
    fixture.topology(screens);
    fixture.controller(true);
    QCOMPARE(capabilityVersion(fixture.primary.options), kPersonalLayoutEnabled ? 2 : 0);
    const auto before = fixture.config.screenLayout("source");
    QCOMPARE(before.screens.size(), nodes);
    const Move move = nodes == 3 ? Move{"third", 0, 1, before.revision} : Move{"receiver", 2, 1, before.revision};
    const auto desired = movedSnapshot(before, move);
    QVERIFY(desired);
    fixture.move(100, move);
    QCOMPARE(fixture.config.screenLayout("source").revision, before.revision);
    QVERIFY(fixture.saveTopology(*desired));
    fixture.complete(1, true);
    const auto after = fixture.config.screenLayout("source");
    QCOMPARE(after.revision, desired->revision);
    QCOMPARE(after.screens, desired->screens);
    QVERIFY(fixture.events.handlers.contains({EventTypes::KeyStateKeyDown, fixture.primary.getEventTarget()}));
  }
}

void ServerTests::screenTopology_rejectsStaleCollisionAndDisconnectedMoves()
{
  using namespace deskflow::layout;
  LayoutServerFixture fixture;
  fixture.topology({{"source", 0, 0}, {"receiver", 1, 0}, {"third", 1, 1}});
  fixture.controller(true);
  const auto before = fixture.config.screenLayout("source");
  fixture.move(1, {"third", 0, 0, before.revision}); // Occupied source.
  fixture.move(2, {"receiver", 3, 0, before.revision}); // Disconnects the graph.
  fixture.move(3, {"third", 0, 1, QString(64, u'0')}); // Stale full revision.
  fixture.move(4, {"unknown", 0, 1, before.revision});
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").revision, before.revision);
  const Move accepted{"third", 0, 1, before.revision};
  fixture.move(5, accepted);
  fixture.move(6, {"source", 2, 0, before.revision}); // Concurrent proposal cannot replace it.
  const auto desired = movedSnapshot(before, accepted);
  QVERIFY(desired);
  QVERIFY(fixture.saveTopology(*desired));
  fixture.complete(1, true);
  QCOMPARE(fixture.config.screenLayout("source").revision, desired->revision);
  fixture.move(7, {"third", 1, 1, before.revision});
  fixture.complete(2, true);
  QCOMPARE(fixture.config.screenLayout("source").revision, desired->revision);
}

void ServerTests::screenTopology_publishesCanonicalIdentityAndUpdatedGraphToAllPeers()
{
  if (!kPersonalLayoutEnabled) QSKIP("The personal build advertises topology synchronization.");
  using namespace deskflow::layout;
  LayoutServerFixture fixture;
  fixture.topology({{"source", 0, 0}, {"receiver", 1, 0}, {"third", 1, 1}});
  Settings::setValue(Settings::Computer::Aliases.arg("receiver"), QStringList{"receiver-alias"});
  QVERIFY(fixture.config.addAlias("receiver", "receiver-alias"));
  fixture.controller(true);
  auto first = std::make_shared<LayoutStreamState>();
  auto second = std::make_shared<LayoutStreamState>();
  auto *receiver = new LayoutRemoteClient("receiver-alias", new LayoutStream(first), &fixture.events);
  fixture.server->adoptClient(receiver);
  QCOMPARE(receiver->layoutVersion(), 2);
  receiver->setLayoutSubscribed(true);
  auto *third = new LayoutRemoteClient("third", new LayoutStream(second), &fixture.events);
  fixture.server->adoptClient(third);
  third->setLayoutSubscribed(true);
  fixture.controller(true);
  for (const auto &state : {first, second}) {
    const auto frames = layoutFrames(state->written);
    QVERIFY(!frames.isEmpty());
    const auto snapshot = snapshotFromJson(frames.back().value("snapshot").toObject());
    QVERIFY(snapshot);
    QCOMPARE(snapshot->screens.size(), 3);
    QVERIFY(snapshot->connectedNames.contains("source"));
    QVERIFY(snapshot->connectedNames.contains("receiver"));
    QVERIFY(snapshot->connectedNames.contains("third"));
    QVERIFY(!snapshot->connectedNames.contains("receiver-alias"));
  }
  QCOMPARE(snapshotFromJson(layoutFrames(first->written).back().value("snapshot").toObject())->localName,
           QStringLiteral("receiver"));
  QCOMPARE(snapshotFromJson(layoutFrames(second->written).back().value("snapshot").toObject())->localName,
           QStringLiteral("third"));
  const auto before = fixture.config.screenLayout("source");
  const Move move{"third", 0, 1, before.revision};
  const auto desired = movedSnapshot(before, move);
  QVERIFY(desired);
  first->written.clear();
  second->written.clear();
  Command request;
  request.kind = Command::Kind::Move;
  request.id = 501;
  request.move = move;
  QVERIFY(fixture.events.dispatchEvent(Event(EventTypes::ScreenLayoutRequest, receiver->getEventTarget(), &request)));
  QVERIFY(fixture.saveTopology(*desired));
  fixture.complete(1, true);
  for (const auto &state : {first, second}) {
    const auto frames = layoutFrames(state->written);
    QVERIFY(!frames.isEmpty());
    const auto snapshot = snapshotFromJson(frames.back().value("snapshot").toObject());
    QVERIFY(snapshot);
    QCOMPARE(snapshot->revision, desired->revision);
    QCOMPARE(snapshot->screens, desired->screens);
    QVERIFY(snapshot->screens.contains({"third", 0, 1}));
  }
  bool acknowledged = false;
  for (const auto &frame : layoutFrames(first->written))
    if (frame.value("id").toInt() == 501 && frame.value("success").toBool()) acknowledged = true;
  QVERIFY(acknowledged);
  first->written.clear();
  second->closed = true;
  QVERIFY(fixture.events.dispatchEvent(Event(EventTypes::ClientProxyDisconnected, third)));
  QCOMPARE(second->writesAfterClose, 0);
  const auto updates = layoutFrames(first->written);
  QVERIFY(!updates.isEmpty());
  const auto disconnected = snapshotFromJson(updates.back().value("snapshot").toObject());
  QVERIFY(disconnected);
  QCOMPARE(disconnected->revision, desired->revision);
  QCOMPARE(disconnected->screens.size(), 3); // Offline configured devices remain visible.
  QVERIFY(!disconnected->connectedNames.contains("third"));
  QVERIFY(disconnected->connectedNames.contains("receiver"));
}
