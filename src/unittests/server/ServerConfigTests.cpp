/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerConfigTests.h"

#include "server/Config.h"

class OnlySystemFilter : public InputFilter::Condition
{
public:
  Condition *clone() const override
  {
    return new OnlySystemFilter();
  }
  std::string format() const override
  {
    return "";
  }

  InputFilter::FilterStatus match(const Event &ev) override
  {
    return ev.getType() == EventTypes::System ? InputFilter::FilterStatus::Activate
                                              : InputFilter::FilterStatus::NoMatch;
  }
};

using namespace deskflow::server;

void ServerConfigTests::equalityCheck()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a != b);

  QVERIFY(b.addComputer("computerB"));
  QVERIFY(a != b);

  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.addComputer("computerC"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(a.connect("computerB", Direction::Left, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerC"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.connect("computerB", Direction::Left, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(a.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(b.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(a.addOption(std::string(), kOptionClipboardSharing, 1));
  QVERIFY(b.addOption(std::string(), kOptionClipboardSharing, 1));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  b.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  QVERIFY(a.addAlias("computerA", "aliasA"));
  QVERIFY(b.addAlias("computerA", "aliasA"));
  /* TODO Fix linking to the proper libs
  NetworkAddress addr1("localhost", 8080);
  addr1.resolve();
  NetworkAddress addr2("localhost", 8080);
  addr2.resolve();
  a.setDeskflowAddress(addr1);
  b.setDeskflowAddress(addr2);
  */
  QVERIFY(a == b);
}

void ServerConfigTests::equalityCheck_diff_options()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(a.addOption("computerA", kOptionClipboardSharing, 0));
  QVERIFY(b.addOption("computerA", kOptionClipboardSharing, 1));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_alias()
{
  Config a(nullptr);
  Config b(nullptr);

  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addAlias("computerA", "aliasA"));
  QVERIFY(a != b);

  QVERIFY(a.addAlias("computerA", "aliasA"));
  QVERIFY(b.addAlias("computerA", "aliasB"));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_filters()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));

  a.getInputFilter()->addFilterRule(InputFilter::Rule{new OnlySystemFilter()});
  QVERIFY(a != b);
}

// TODO FIX
/*
void ServerConfigTests::equalityCheck_diff_address()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(b.addComputer("computerA"));
  a.setDeskflowAddress(NetworkAddress(8000));
  b.setDeskflowAddress(NetworkAddress(9000));
  QVERIFY(a != b);
}
*/

void ServerConfigTests::equalityCheck_diff_neighbours1()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(a != b);
  QVERIFY(b != a);
}

void ServerConfigTests::equalityCheck_diff_neighbours2()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.25f, "computerB", 0.25f, 1.0f));
  QVERIFY(a != b);
}

void ServerConfigTests::equalityCheck_diff_neighbours3()
{
  Config a(nullptr);
  Config b(nullptr);
  QVERIFY(a.addComputer("computerA"));
  QVERIFY(a.addComputer("computerB"));
  QVERIFY(a.addComputer("computerC"));
  QVERIFY(a.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerB", 0.5f, 1.0f));
  QVERIFY(b.addComputer("computerA"));
  QVERIFY(b.addComputer("computerB"));
  QVERIFY(b.addComputer("computerC"));
  QVERIFY(b.connect("computerA", Direction::Bottom, 0.0f, 0.5f, "computerC", 0.5f, 1.0f));
  QVERIFY(a != b);
}

QTEST_MAIN(ServerConfigTests)

void ServerConfigTests::screenLayout_pair_data()
{
  QTest::addColumn<int>("direction");
  QTest::newRow("left") << 1;
  QTest::newRow("right") << 2;
  QTest::newRow("above") << 3;
  QTest::newRow("below") << 4;
}

void ServerConfigTests::screenLayout_pair()
{
  QFETCH(int, direction);
  Config config(nullptr);
  QVERIFY(config.addComputer("source"));
  QVERIFY(config.addComputer("receiver"));
  const auto desired = static_cast<deskflow::layout::Direction>(direction);
  QVERIFY(config.connect("source", static_cast<Direction>(desired), 0, 1, "receiver", 0, 1));
  QVERIFY(config.connect("receiver", static_cast<Direction>(deskflow::layout::opposite(desired)), 0, 1, "source", 0, 1));
  const auto actual = config.screenLayout("source");
  QVERIFY(actual.editable);
  QCOMPARE(actual.serverName, QStringLiteral("source"));
  QCOMPARE(actual.clientName, QStringLiteral("receiver"));
  QCOMPARE(actual.direction, desired);
  QVERIFY(actual.error.isEmpty());
}

void ServerConfigTests::screenLayout_rejectsCustomLinks()
{
  Config config(nullptr);
  QVERIFY(config.addComputer("source"));
  QVERIFY(config.addComputer("receiver"));
  QVERIFY(!config.screenLayout("source").editable);
  QVERIFY(config.connect("source", Direction::Right, 0, 1, "receiver", 0, 1));
  QVERIFY(!config.screenLayout("source").editable); // One-way route.
  QVERIFY(config.connect("receiver", Direction::Right, 0, 1, "source", 0, 1));
  QVERIFY(!config.screenLayout("source").editable); // Both links face right.
  QVERIFY(config.disconnect("receiver", Direction::Right));
  QVERIFY(config.connect("receiver", Direction::Left, 0, 1, "source", 0, 1));
  QVERIFY(config.screenLayout("source").editable);
  QVERIFY(config.disconnect("source", Direction::Right));
  QVERIFY(config.connect("source", Direction::Right, 0, 0.5, "receiver", 0, 1));
  QVERIFY(!config.screenLayout("source").editable); // Partial source edge.
  QVERIFY(config.disconnect("source", Direction::Right));
  QVERIFY(config.connect("source", Direction::Right, 0, 1, "receiver", 0.25, 0.75));
  QVERIFY(!config.screenLayout("source").editable); // Partial target edge.
  QVERIFY(config.disconnect("source", Direction::Right));
  QVERIFY(config.connect("source", Direction::Right, 0, 1, "receiver", 0, 1));
  QVERIFY(config.connect("source", Direction::Top, 0, 1, "receiver", 0, 1));
  QVERIFY(!config.screenLayout("source").editable); // Extra route.
  QVERIFY(config.disconnect("source", Direction::Top));
  QVERIFY(config.addComputer("third"));
  QVERIFY(!config.screenLayout("source").editable);
  QVERIFY(!config.screenLayout("unknown").editable);
}

void ServerConfigTests::screenLayout_canonicalizesAliases()
{
  Config config(nullptr);
  QVERIFY(config.addComputer("source"));
  QVERIFY(config.addComputer("receiver"));
  QVERIFY(config.addAlias("source", "source-alias"));
  QVERIFY(config.addAlias("receiver", "receiver-alias"));
  QVERIFY(config.connect("source", Direction::Top, 0, 1, "receiver-alias", 0, 1));
  QVERIFY(config.connect("receiver", Direction::Bottom, 0, 1, "source-alias", 0, 1));
  const auto layout = config.screenLayout("source-alias");
  QVERIFY(layout.editable);
  QCOMPARE(layout.serverName, QStringLiteral("source"));
  QCOMPARE(layout.clientName, QStringLiteral("receiver"));
  QCOMPARE(layout.direction, deskflow::layout::Direction::Above);
}

void ServerConfigTests::screenLayout_threeAndFourNodes()
{
  Config config(nullptr);
  for (const auto name : {"source", "left", "above", "corner"}) QVERIFY(config.addComputer(name));
  const auto link = [&](const char *a, Direction direction, const char *b) {
    QVERIFY(config.connect(a, direction, 0, 1, b, 0, 1));
    QVERIFY(config.connect(b, static_cast<Direction>(deskflow::layout::opposite(
        static_cast<deskflow::layout::Direction>(direction))), 0, 1, a, 0, 1));
  };
  link("source", Direction::Left, "left");
  link("source", Direction::Top, "above");
  link("above", Direction::Left, "corner");
  // A square with a missing touching edge cannot promise full grid crossings.
  QVERIFY(!config.screenLayout("source").editable);
  link("left", Direction::Top, "corner");
  const auto square = config.screenLayout("source");
  QVERIFY(square.editable);
  QCOMPARE(square.protocolVersion, 2);
  QCOMPARE(square.screens.size(), 4);
  QCOMPARE(square.revision, deskflow::layout::revisionFor(square));
  QVERIFY(deskflow::layout::validateTopology(square));
  QCOMPARE(square.direction, deskflow::layout::Direction::Unknown);
  auto moved = deskflow::layout::movedSnapshot(square, {"corner", -2, 0, square.revision});
  QVERIFY(moved);
  QCOMPARE(moved->screens.size(), 4);
  QVERIFY(moved->revision != square.revision);

  Config three(nullptr);
  for (const auto name : {"source", "receiver", "third"}) QVERIFY(three.addComputer(name));
  QVERIFY(three.connect("source", Direction::Right, 0, 1, "receiver", 0, 1));
  QVERIFY(three.connect("receiver", Direction::Left, 0, 1, "source", 0, 1));
  QVERIFY(three.connect("receiver", Direction::Bottom, 0, 1, "third", 0, 1));
  QVERIFY(three.connect("third", Direction::Top, 0, 1, "receiver", 0, 1));
  const auto layout = three.screenLayout("source");
  QVERIFY(layout.editable);
  QCOMPARE(layout.screens.size(), 3);
  QVERIFY(layout.screens.contains({"third", 1, 1}));
}

void ServerConfigTests::screenLayout_rejectsInconsistentAndIncompleteGraph()
{
  Config config(nullptr);
  for (const auto name : {"source", "right", "other"}) QVERIFY(config.addComputer(name));
  QVERIFY(config.connect("source", Direction::Right, 0, 1, "right", 0, 1));
  QVERIFY(config.connect("right", Direction::Left, 0, 1, "source", 0, 1));
  QVERIFY(config.connect("right", Direction::Right, 0, 1, "other", 0, 1));
  QVERIFY(config.connect("other", Direction::Left, 0, 1, "right", 0, 1));
  QVERIFY(config.screenLayout("source").editable);
  // Closing a horizontal chain into a vertical cycle gives two coordinates to source.
  QVERIFY(config.connect("other", Direction::Top, 0, 1, "source", 0, 1));
  QVERIFY(config.connect("source", Direction::Bottom, 0, 1, "other", 0, 1));
  QVERIFY(!config.screenLayout("source").editable);
}
