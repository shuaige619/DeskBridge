/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXShortcutMapperTests.h"

#include "base/EventQueue.h"
#include "deskflow/KeyState.h"
#include "platform/OSXShortcutMapper.h"

#include <QTest>

#include <algorithm>
#include <set>

namespace {

constexpr auto kCodex = "com.openai.codex";
constexpr auto kLark = "com.electron.lark";
constexpr auto kTerminal = "com.apple.Terminal";
constexpr KeyButton kControlButton = 10;
constexpr KeyButton kCommandButton = 11;
constexpr KeyButton kCopyButton = 12;
constexpr KeyButton kPasteButton = 13;
constexpr KeyButton kSpaceButton = 14;
constexpr KeyButton kRightControlButton = 15;
constexpr KeyButton kSourceButton = 16;
constexpr KeyButton kFunctionButton = 17;
constexpr KeyButton kLeftButton = 18;
constexpr KeyButton kShiftButton = 19;
constexpr KeyButton kAltButton = 20;

OSXShortcutMapper configuredMapper()
{
  OSXShortcutMapper mapper;
  mapper.configure(true, {kCodex, kLark});
  return mapper;
}

void buildShortcutKeyMap(deskflow::KeyMap &map)
{
  const auto add = [&map](KeyID id, KeyButton button, KeyModifierMask generates = 0) {
    deskflow::KeyMap::KeyItem item;
    item.m_id = id;
    item.m_button = button;
    item.m_generates = generates;
    map.addKeyEntry(item);
  };
  add(kKeyControl_L, kControlButton, KeyModifierControl);
  add(kKeySuper_L, kCommandButton, KeyModifierSuper);
  add(kKeyControl_R, kRightControlButton, KeyModifierControl);
  add(kKeyShift_L, kShiftButton, KeyModifierShift);
  add(kKeyAlt_L, kAltButton, KeyModifierAlt);
  add('c', kCopyButton);
  add('v', kPasteButton);
  add('x', kSourceButton);
  add(kKeyF1, kFunctionButton);
  add(kKeyLeft, kLeftButton);
  add(' ', kSpaceButton);
  deskflow::KeyMap::KeyItem shifted;
  shifted.m_id = 'V';
  shifted.m_button = kPasteButton;
  shifted.m_required = KeyModifierShift;
  shifted.m_sensitive = KeyModifierShift;
  map.addKeyEntry(shifted);
  map.finish();
}

// Exercise the real KeyMap/KeyState modifier synthesis without posting any OS
// events or accessing the user's keyboard, foreground app or clipboard.
class RecordingKeyState : public KeyState
{
public:
  struct Event
  {
    KeyButton button;
    bool down;
    KeyModifierMask modifiers;
  };

  RecordingKeyState(IEventQueue *events, deskflow::KeyMap &map) : KeyState(events, map, {"en"}, false)
  {
  }

  int32_t pollActiveGroup() const override
  {
    return 0;
  }

  KeyModifierMask pollActiveModifiers() const override
  {
    return modifiers;
  }

  bool fakeCtrlAltDel() override
  {
    return false;
  }

  void pollPressedKeys(KeyButtonSet &keys) const override
  {
    keys.insert(pressed.begin(), pressed.end());
  }

  bool sawPress(KeyButton button, KeyModifierMask mask) const
  {
    return std::any_of(events.begin(), events.end(), [button, mask](const auto &event) {
      return event.button == button && event.down && event.modifiers == mask;
    });
  }

  KeyModifierMask modifiers = 0;
  KeyModifierMask lockedModifiers = 0;
  std::set<KeyButton> pressed;
  std::vector<Event> events;

protected:
  void getKeyMap(deskflow::KeyMap &) override
  {
  }

  void fakeKey(const Keystroke &key) override
  {
    if (key.m_type != Keystroke::KeyType::Button) {
      return;
    }
    const auto button = key.m_data.m_button.m_button;
    const auto down = key.m_data.m_button.m_press;
    if (down) {
      pressed.insert(button);
    } else {
      pressed.erase(button);
    }
    modifiers = lockedModifiers |
                (pressed.contains(kControlButton) || pressed.contains(kRightControlButton) ? KeyModifierControl : 0) |
                (pressed.contains(kCommandButton) ? KeyModifierSuper : 0) |
                (pressed.contains(kShiftButton) ? KeyModifierShift : 0) |
                (pressed.contains(kAltButton) ? KeyModifierAlt : 0);
    events.push_back({button, down, modifiers});
  }
};

} // namespace

void OSXShortcutMapperTests::initTestCase()
{
  m_arch.init();
}

void OSXShortcutMapperTests::exactShortcuts_data()
{
  QTest::addColumn<KeyID>("id");
  QTest::addColumn<KeyModifierMask>("mask");
  QTest::addColumn<QString>("app");
  QTest::addColumn<bool>("enabled");
  QTest::addColumn<KeyModifierMask>("expected");

  const auto codex = QString::fromLatin1(kCodex);
  const auto control = KeyModifierControl;
  const auto command = KeyModifierSuper;
  QTest::newRow("copy") << KeyID('c') << control << codex << true << command;
  QTest::newRow("paste-lark") << KeyID('v') << control << QString::fromLatin1(kLark) << true << command;
  QTest::newRow("caps-copy") << KeyID('C') << (control | KeyModifierCapsLock) << codex << true
                             << (command | KeyModifierCapsLock);
  QTest::newRow("caps-paste") << KeyID('V') << (control | KeyModifierCapsLock) << codex << true
                              << (command | KeyModifierCapsLock);
  QTest::newRow("lock-states") << KeyID('c') << (control | KeyModifierNumLock | KeyModifierScrollLock) << codex
                               << true << (command | KeyModifierNumLock | KeyModifierScrollLock);
  QTest::newRow("disabled") << KeyID('c') << control << codex << false << control;
  QTest::newRow("terminal") << KeyID('c') << control << QString::fromLatin1(kTerminal) << true << control;
  QTest::newRow("missing-app") << KeyID('c') << control << QString() << true << control;
  QTest::newRow("similar-bundle-id") << KeyID('c') << control << QStringLiteral("com.openai.codex.helper") << true
                                    << control;
  QTest::newRow("input-switch") << KeyID(' ') << control << codex << true << control;
  QTest::newRow("other-key") << KeyID('a') << control << codex << true << control;
  QTest::newRow("plain-copy") << KeyID('c') << KeyModifierMask(0) << codex << true << KeyModifierMask(0);
  for (const auto [name, extra] : {
           std::pair{"shift", KeyModifierShift}, std::pair{"alt", KeyModifierAlt},
           std::pair{"command", KeyModifierSuper}, std::pair{"meta", KeyModifierMeta},
           std::pair{"altgr", KeyModifierAltGr}, std::pair{"level5", KeyModifierLevel5Lock},
           std::pair{"unknown-modifier", KeyModifierMask(0x8000)}
       }) {
    QTest::newRow(name) << KeyID('c') << (control | extra) << codex << true << (control | extra);
  }
  QTest::newRow("alt-paste") << KeyID('v') << (control | KeyModifierAlt) << codex << true
                             << (control | KeyModifierAlt);
}

void OSXShortcutMapperTests::exactShortcuts()
{
  QFETCH(KeyID, id);
  QFETCH(KeyModifierMask, mask);
  QFETCH(QString, app);
  QFETCH(bool, enabled);
  QFETCH(KeyModifierMask, expected);
  auto mapper = configuredMapper();
  mapper.configure(enabled, {kCodex, kLark});
  QCOMPARE(mapper.keyDown(id, mask, 1, app.toStdString()), expected);
  QCOMPARE(mapper.keyRepeat(id, mask, 1), expected);
  mapper.keyUp(1);
  QCOMPARE(mapper.keyRepeat(id, mask, 1), mask);
}

void OSXShortcutMapperTests::repeatsKeepInitialAppAndSettings()
{
  auto mapper = configuredMapper();
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kCodex), KeyModifierSuper);
  mapper.configure(false, {});
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kTerminal), KeyModifierSuper);
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl, 1), KeyModifierSuper);
}

void OSXShortcutMapperTests::unconvertedPressStaysUnconvertedAfterFocusChange()
{
  auto mapper = configuredMapper();
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kTerminal), KeyModifierControl);
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kCodex), KeyModifierControl);
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl, 1), KeyModifierControl);
}

void OSXShortcutMapperTests::repeatKeepsMappedOutputWhenModifiersChange()
{
  auto mapper = configuredMapper();
  mapper.keyDown('c', KeyModifierControl, 1, kCodex);
  QCOMPARE(mapper.keyRepeat('c', 0, 1), KeyModifierSuper);
  QCOMPARE(mapper.keyRepeat('C', KeyModifierControl | KeyModifierShift, 1), KeyModifierSuper);
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl | KeyModifierAlt, 1), KeyModifierSuper);
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl, 1), KeyModifierSuper);
}

void OSXShortcutMapperTests::keyUpAllowsFreshDecision()
{
  auto mapper = configuredMapper();
  mapper.keyDown('c', KeyModifierControl, 1, kCodex);
  mapper.keyUp(1);
  QVERIFY(!mapper.isKeyDown(1));
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kTerminal), KeyModifierControl);
  mapper.keyUp(1);
  mapper.keyUp(1); // Duplicate or late releases must be harmless.
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kCodex), KeyModifierSuper);
}

void OSXShortcutMapperTests::heldKeysHaveIndependentDecisions()
{
  auto mapper = configuredMapper();
  mapper.keyDown('c', KeyModifierControl, 1, kCodex);
  mapper.keyDown('v', KeyModifierControl, 2, kTerminal);
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl, 1), KeyModifierSuper);
  QCOMPARE(mapper.keyRepeat('v', KeyModifierControl, 2), KeyModifierControl);
  mapper.keyUp(1);
  QCOMPARE(mapper.keyRepeat('v', KeyModifierControl, 2), KeyModifierControl);
}

void OSXShortcutMapperTests::resetForgetsHeldKeys()
{
  auto mapper = configuredMapper();
  mapper.keyDown('c', KeyModifierControl, 1, kCodex);
  mapper.keyDown('v', KeyModifierControl, 2, kLark);
  mapper.reset();
  QVERIFY(!mapper.isKeyDown(1));
  QVERIFY(!mapper.isKeyDown(2));
  QCOMPARE(mapper.keyRepeat('c', KeyModifierControl, 1), KeyModifierControl);
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 1, kTerminal), KeyModifierControl);
}

void OSXShortcutMapperTests::invalidButtonIsNotMapped()
{
  auto mapper = configuredMapper();
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 0, kCodex), KeyModifierControl);
  QVERIFY(!mapper.isKeyDown(0));
}

void OSXShortcutMapperTests::keyStateRestoresControlAndPreservesInputSwitch()
{
  deskflow::KeyMap keyMap;
  buildShortcutKeyMap(keyMap);
  EventQueue queue;
  RecordingKeyState state(&queue, keyMap);
  auto mapper = configuredMapper();
  state.fakeKeyDown(kKeyControl_L, KeyModifierControl, 101, "en");
  QCOMPARE(state.modifiers, KeyModifierControl);

  const auto copyMask = mapper.keyDown('c', KeyModifierControl, 102, kCodex);
  state.fakeKeyDown('c', copyMask, 102, "en");
  QVERIFY(state.sawPress(kCopyButton, KeyModifierSuper));
  QCOMPARE(state.modifiers, KeyModifierControl);
  QVERIFY(!state.pressed.contains(kCommandButton));

  state.events.clear();
  QVERIFY(state.fakeKeyRepeat('c', mapper.keyRepeat('c', KeyModifierControl, 102), 2, 102, "en"));
  QVERIFY(state.sawPress(kCopyButton, KeyModifierSuper));
  QCOMPARE(state.modifiers, KeyModifierControl);
  mapper.keyUp(102);
  QVERIFY(state.fakeKeyUp(102));
  QVERIFY(!state.pressed.contains(kCopyButton));

  state.events.clear();
  const auto inputMask = mapper.keyDown(' ', KeyModifierControl, 103, kCodex);
  state.fakeKeyDown(' ', inputMask, 103, "en");
  QVERIFY(state.sawPress(kSpaceButton, KeyModifierControl));
  QVERIFY(!state.sawPress(kCommandButton, KeyModifierSuper));
  mapper.keyUp(103);
  QVERIFY(state.fakeKeyUp(103));
  QVERIFY(state.fakeKeyUp(101));
  QVERIFY(state.pressed.empty());
  QCOMPARE(state.modifiers, KeyModifierMask(0));
}

void OSXShortcutMapperTests::fullShortcutKeyStatePreservesShiftAndInputSwitch()
{
  deskflow::KeyMap keyMap;
  buildShortcutKeyMap(keyMap);
  EventQueue queue;
  RecordingKeyState state(&queue, keyMap);
  auto config = deskflow::shortcuts::defaults();
  config.rules.clear();
  config.habit = deskflow::shortcuts::Habit::PC;
  config.fullShortcuts = true;
  config.allApplications = true;
  OSXShortcutMapper mapper;
  mapper.configure(config, deskflow::shortcuts::DeviceOS::Linux, deskflow::shortcuts::DeviceOS::MacOS);
  const auto press = [&](KeyID key, KeyModifierMask mask, KeyButton button) {
    const auto chord = mapper.mapKeyDown({key, mask}, button, {});
    state.fakeKeyDown(chord.key, chord.modifiers, button, "en");
  };
  press(kKeyControl_L, KeyModifierControl, 101);
  press(kKeyShift_L, KeyModifierControl | KeyModifierShift, 102);
  press('V', KeyModifierControl | KeyModifierShift, 103);
  QVERIFY(state.sawPress(kPasteButton, KeyModifierSuper | KeyModifierShift));
  QCOMPARE(state.modifiers, KeyModifierControl | KeyModifierShift);
  QVERIFY(!state.pressed.contains(kCommandButton));
  mapper.keyUp(103);
  QVERIFY(state.fakeKeyUp(103));
  mapper.keyUp(102);
  QVERIFY(state.fakeKeyUp(102));
  QCOMPARE(state.modifiers, KeyModifierControl);
  state.events.clear();
  press(' ', KeyModifierControl, 104);
  QVERIFY(state.sawPress(kSpaceButton, KeyModifierControl));
  QVERIFY(!state.sawPress(kCommandButton, KeyModifierSuper));
  state.fakeAllKeysUp();
  mapper.reset();
  QVERIFY(state.pressed.empty());
  QCOMPARE(state.modifiers, KeyModifierMask(0));
}

void OSXShortcutMapperTests::shiftTapReusesReleasedButtonWithoutLeavingModifiers_data()
{
  QTest::addColumn<KeyModifierMask>("locks");
  QTest::newRow("no-locks") << KeyModifierMask(0);
  QTest::newRow("caps-lock") << KeyModifierCapsLock;
  QTest::newRow("num-and-scroll-lock") << (KeyModifierNumLock | KeyModifierScrollLock);
  QTest::newRow("all-locks") << (KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock);
}

void OSXShortcutMapperTests::shiftTapReusesReleasedButtonWithoutLeavingModifiers()
{
  QFETCH(KeyModifierMask, locks);
  deskflow::KeyMap keyMap;
  buildShortcutKeyMap(keyMap);
  EventQueue queue;
  RecordingKeyState state(&queue, keyMap);
  state.lockedModifiers = locks;
  state.modifiers = locks;
  state.updateKeyState(); // Polls only this in-memory double, never the OS.
  constexpr KeyButton serverButton = 101;
  static_assert(serverButton > 0 && serverButton < IKeyState::s_numButtons);

  // The real remote Shift must be forwarded and successfully released before
  // its now-free server slot can temporarily identify the synthetic Space.
  state.fakeKeyDown(kKeyShift_L, KeyModifierShift | locks, serverButton, "en");
  QCOMPARE(state.events.size(), size_t(1));
  QCOMPARE(state.events[0].button, kShiftButton);
  QVERIFY(state.events[0].down);
  QCOMPARE(state.events[0].modifiers, KeyModifierShift | locks);
  QVERIFY(state.fakeKeyUp(serverButton));
  QCOMPARE(state.events.size(), size_t(2));
  QCOMPARE(state.events[1].button, kShiftButton);
  QVERIFY(!state.events[1].down);
  QCOMPARE(state.events[1].modifiers, locks);
  QCOMPARE(state.getActiveModifiers(), locks);
  QVERIFY(state.pressed.empty());

  // Bypass ShortcutMapper exactly as the input-method pulse must. KeyMap
  // appends Ctrl's undo to the down sequence, rather than relying on Space up.
  state.events.clear();
  state.fakeKeyDown(' ', KeyModifierControl | locks, serverButton, "");
  QCOMPARE(state.events.size(), size_t(3));
  QCOMPARE(state.events[0].button, kControlButton);
  QVERIFY(state.events[0].down);
  QCOMPARE(state.events[0].modifiers, KeyModifierControl | locks);
  QCOMPARE(state.events[1].button, kSpaceButton);
  QVERIFY(state.events[1].down);
  QCOMPARE(state.events[1].modifiers, KeyModifierControl | locks);
  QCOMPARE(state.events[2].button, kControlButton);
  QVERIFY(!state.events[2].down);
  QCOMPARE(state.events[2].modifiers, locks);
  QCOMPARE(state.getActiveModifiers(), locks);
  QCOMPARE(state.modifiers, locks);
  QVERIFY(state.pressed == std::set<KeyButton>{kSpaceButton});

  QVERIFY(state.fakeKeyUp(serverButton));
  QCOMPARE(state.events.size(), size_t(4));
  QCOMPARE(state.events[3].button, kSpaceButton);
  QVERIFY(!state.events[3].down);
  QCOMPARE(state.events[3].modifiers, locks);
  QCOMPARE(state.getActiveModifiers(), locks);
  QCOMPARE(state.modifiers, locks);
  QCOMPARE(state.getKeyState(kControlButton), 0);
  QCOMPARE(state.getKeyState(kShiftButton), 0);
  QCOMPARE(state.getKeyState(kSpaceButton), 0);
  QVERIFY(state.pressed.empty());
  QVERIFY(!state.fakeKeyUp(serverButton));
  QVERIFY(!state.fakeKeyRepeat(' ', KeyModifierControl, 1, serverButton, "en"));
  QCOMPARE(state.events.size(), size_t(4)); // No server-slot mapping remains.
}

void OSXShortcutMapperTests::keyStateResetReleasesHeldKeys()
{
  deskflow::KeyMap keyMap;
  buildShortcutKeyMap(keyMap);
  EventQueue queue;
  RecordingKeyState state(&queue, keyMap);
  auto mapper = configuredMapper();
  state.fakeKeyDown(kKeyControl_L, KeyModifierControl, 101, "en");
  state.fakeKeyDown('c', mapper.keyDown('c', KeyModifierControl, 102, kCodex), 102, "en");
  state.fakeKeyDown('v', mapper.keyDown('v', KeyModifierControl, 103, kLark), 103, "en");
  QVERIFY(state.pressed.contains(kCopyButton));
  QVERIFY(state.pressed.contains(kPasteButton));

  // This is the client leave/disconnect cleanup: release synthesized keys, then
  // discard the policy decisions before a new connection can reuse buttons.
  state.fakeAllKeysUp();
  mapper.reset();
  QVERIFY(state.pressed.empty());
  QCOMPARE(state.modifiers, KeyModifierMask(0));
  QVERIFY(!state.fakeKeyRepeat('c', mapper.keyRepeat('c', KeyModifierControl, 102), 1, 102, "en"));
  QVERIFY(!state.fakeKeyUp(102));
  QCOMPARE(mapper.keyDown('c', KeyModifierControl, 102, kTerminal), KeyModifierControl);
}

void OSXShortcutMapperTests::keyStateRestoresBothControlKeys()
{
  deskflow::KeyMap keyMap;
  buildShortcutKeyMap(keyMap);
  EventQueue queue;
  RecordingKeyState state(&queue, keyMap);
  auto mapper = configuredMapper();
  state.fakeKeyDown(kKeyControl_L, KeyModifierControl, 101, "en");
  state.fakeKeyDown(kKeyControl_R, KeyModifierControl, 102, "en");
  state.fakeKeyDown('c', mapper.keyDown('c', KeyModifierControl, 103, kCodex), 103, "en");
  QVERIFY(state.sawPress(kCopyButton, KeyModifierSuper));
  QVERIFY(state.pressed.contains(kControlButton));
  QVERIFY(state.pressed.contains(kRightControlButton));
  QVERIFY(!state.pressed.contains(kCommandButton));
  QCOMPARE(state.modifiers, KeyModifierControl);

  mapper.keyUp(103);
  QVERIFY(state.fakeKeyUp(103));
  QVERIFY(state.fakeKeyUp(101));
  QCOMPARE(state.modifiers, KeyModifierControl);
  QVERIFY(state.fakeKeyUp(102));
  QCOMPARE(state.modifiers, KeyModifierMask(0));
  QVERIFY(state.pressed.empty());
}

void OSXShortcutMapperTests::genericRulesRespectAppBoundaries()
{
  using namespace deskflow::shortcuts;
  auto config = defaults();
  config.apps = {QString::fromLatin1(kCodex)};
  config.rules = {{QStringLiteral("custom"), QStringLiteral("Custom"), {'x', KeyModifierControl},
                   {'v', KeyModifierSuper}, true, {QString::fromLatin1(kLark)}}};
  OSXShortcutMapper mapper;
  mapper.configure(config);
  const Chord input{'x', KeyModifierControl};
  QVERIFY(!mapper.isCandidate(input));
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kLark)) == input);
  mapper.keyUp(1);

  config.apps.push_back(QString::fromLatin1(kLark));
  mapper.configure(config);
  QVERIFY(mapper.isCandidate(input));
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kLark)) == (Chord{'v', KeyModifierSuper}));
  mapper.keyUp(1);
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kCodex)) == input);
  mapper.keyUp(1);

  config.apps.clear();
  mapper.configure(config);
  QVERIFY(!mapper.isCandidate(input));
  config.apps = {QString::fromLatin1(kLark)};
  config.rules.first().apps.clear();
  mapper.configure(config);
  QVERIFY(!mapper.isCandidate(input));
  config.rules.first().apps = config.apps;
  config.rules.first().enabled = false;
  mapper.configure(config);
  QVERIFY(!mapper.isCandidate(input));
  config.rules.first().enabled = true;
  config.enabled = false;
  mapper.configure(config);
  QVERIFY(!mapper.isCandidate(input));

  config.enabled = true;
  config.rules.first().input = {' ', KeyModifierControl};
  mapper.configure(config); // Invalid DTOs also fail closed, without legacy fallback.
  QVERIFY(!mapper.isCandidate({' ', KeyModifierControl}));
  const Chord inputSwitch{' ', KeyModifierControl | KeyModifierCapsLock};
  QVERIFY(mapper.mapKeyDown(inputSwitch, 2, QString::fromLatin1(kLark)) == inputSwitch);
}

void OSXShortcutMapperTests::genericPressKeepsOutputAcrossFocusAndConfigChanges()
{
  using namespace deskflow::shortcuts;
  Configuration config;
  config.apps = {QString::fromLatin1(kCodex)};
  config.rules = {{QStringLiteral("custom"), QStringLiteral("Custom"), {'x', KeyModifierControl | KeyModifierShift},
                   {kKeyF20, KeyModifierAlt}, true, config.apps}};
  OSXShortcutMapper mapper;
  mapper.configure(config);
  const Chord input{'X', KeyModifierControl | KeyModifierShift | KeyModifierCapsLock};
  const Chord expected{kKeyF20, KeyModifierAlt | KeyModifierCapsLock};
  QVERIFY(mapper.isCandidate(input));
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kCodex)) == expected);
  config.enabled = false;
  config.rules.first().output = {'c', KeyModifierSuper};
  mapper.configure(config);
  QVERIFY(mapper.mapKeyRepeat({'v', 0}, 1) == expected);
  QVERIFY(mapper.mapKeyDown({'v', 0}, 1, QString::fromLatin1(kTerminal)) == expected);
  mapper.keyUp(1);
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kCodex)) == input);
  mapper.reset();
  QVERIFY(mapper.mapKeyRepeat(input, 1) == input);
}

void OSXShortcutMapperTests::unmappedRepeatRetainsNativeSemantics()
{
  using deskflow::shortcuts::Chord;
  auto mapper = configuredMapper();
  const Chord input{'c', KeyModifierControl};
  QVERIFY(mapper.mapKeyDown(input, 1, QString::fromLatin1(kTerminal)) == input);
  const Chord changed{'V', KeyModifierControl | KeyModifierShift};
  QVERIFY(mapper.mapKeyRepeat(changed, 1) == changed);
  QVERIFY(mapper.mapKeyDown(changed, 1, QString::fromLatin1(kCodex)) == changed);
}

void OSXShortcutMapperTests::keyStatePairsChangedOutputKeyAndReleasesOnReset()
{
  using namespace deskflow::shortcuts;
  for (const Chord output : {Chord{'v', KeyModifierSuper}, Chord{'v', 0},
                            Chord{'v', KeyModifierSuper | KeyModifierShift}, Chord{kKeyLeft, KeyModifierSuper},
                            Chord{kKeyF1, KeyModifierAlt}}) {
    deskflow::KeyMap keyMap;
    buildShortcutKeyMap(keyMap);
    EventQueue queue;
    RecordingKeyState state(&queue, keyMap);
    Configuration config;
    config.apps = {QString::fromLatin1(kCodex)};
    config.rules = {{QStringLiteral("custom"), QStringLiteral("Custom"), {'x', KeyModifierControl}, output, true,
                     config.apps}};
    OSXShortcutMapper mapper;
    mapper.configure(config);
    state.fakeKeyDown(kKeyControl_L, KeyModifierControl, 101, "en");
    const auto down = mapper.mapKeyDown({'x', KeyModifierControl}, 102, QString::fromLatin1(kCodex));
    state.fakeKeyDown(down.key, down.modifiers, 102, "en");
    const auto expectedButton = output.key == kKeyLeft ? kLeftButton
                                : output.key == kKeyF1 ? kFunctionButton
                                                     : kPasteButton;
    QVERIFY(state.sawPress(expectedButton, output.modifiers));
    QVERIFY(!state.pressed.contains(kSourceButton));
    QCOMPARE(state.modifiers, KeyModifierControl);
    QVERIFY(!state.pressed.contains(kCommandButton));
    QVERIFY(!state.pressed.contains(kShiftButton));
    QVERIFY(!state.pressed.contains(kAltButton));

    config.enabled = false;
    mapper.configure(config);
    const auto repeat = mapper.mapKeyRepeat({'c', 0}, 102);
    QVERIFY(repeat == down);
    state.events.clear();
    QVERIFY(state.fakeKeyRepeat(repeat.key, repeat.modifiers, 2, 102, "en"));
    QVERIFY(state.sawPress(expectedButton, output.modifiers));
    mapper.keyUp(102);
    QVERIFY(state.fakeKeyUp(102));
    QVERIFY(!state.pressed.contains(expectedButton));
    QVERIFY(state.fakeKeyUp(101));
    QVERIFY(state.pressed.empty());

    config.enabled = true;
    mapper.configure(config);
    state.fakeKeyDown(kKeyControl_L, KeyModifierControl, 101, "en");
    const auto second = mapper.mapKeyDown({'x', KeyModifierControl}, 102, QString::fromLatin1(kCodex));
    state.fakeKeyDown(second.key, second.modifiers, 102, "en");
    state.fakeAllKeysUp();
    mapper.reset();
    QVERIFY(state.pressed.empty());
    QCOMPARE(state.modifiers, KeyModifierMask(0));
    QVERIFY(!state.fakeKeyUp(102));
    QVERIFY(!state.fakeKeyRepeat(second.key, second.modifiers, 1, 102, "en"));
  }
}

QTEST_GUILESS_MAIN(OSXShortcutMapperTests)
