/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "MockEventQueue.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "common/ShortcutRules.h"
#include "deskflow/Computer.h"
#include "deskflow/PlatformComputer.h"
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>
#include <vector>

namespace sc = deskflow::shortcuts;

// Records calls at the real shared Computer/platform boundary. No native input,
// display, clipboard, child process or IPC is opened by this test double.
class RecordingComputer : public PlatformComputer
{
public:
  RecordingComputer(IEventQueue *events, bool primary) : PlatformComputer(events), primary(primary) {}
  bool primary;
  bool mayLeave = true;
  std::string app = "test.app";
  mutable int appQueries = 0;
  sc::Chord down, repeated;
  KeyButton released = 0;
  int allReleased = 0;
  bool releaseSucceeds = true;
  KeyModifierMask remainingModifiers = 0;
  struct KeyEvent { bool down; KeyID key; KeyModifierMask modifiers; KeyButton button; };
  std::vector<KeyEvent> keyEvents;
  int inputSwitches() const {
    return int(std::count_if(keyEvents.begin(),keyEvents.end(),[](const auto &event) {
      return event.down && event.key==' ' && (event.modifiers & KeyModifierControl);
    }));
  }
  std::string getForegroundApplication() const override { ++appQueries; return app; }
  bool isPrimary() const override { return primary; }
  void *getEventTarget() const override { return nullptr; }
  bool getClipboard(ClipboardID, IClipboard *) const override { return false; }
  void getShape(int32_t &x, int32_t &y, int32_t &w, int32_t &h) const override { x=y=0; w=h=100; }
  void getCursorPos(int32_t &x, int32_t &y) const override { x=y=0; }
  void enable() override {}
  void disable() override {}
  void enter() override {}
  bool canLeave() override { return mayLeave; }
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
  void getCursorCenter(int32_t &x, int32_t &y) const override { x=y=50; }
  void fakeMouseButton(ButtonID, bool) override {}
  void fakeMouseMove(int32_t, int32_t) override {}
  void fakeMouseRelativeMove(int32_t, int32_t) const override {}
  void fakeMouseWheel(ScrollDelta) const override {}
  void updateKeyMap() override {}
  void updateKeyState() override {}
  void setHalfDuplexMask(KeyModifierMask) override {}
  void fakeKeyDown(KeyID key, KeyModifierMask mods, KeyButton button, const std::string &) override {
    down={key,mods}; keyEvents.push_back({true,key,mods,button});
  }
  bool fakeKeyRepeat(KeyID key, KeyModifierMask mods, int32_t, KeyButton, const std::string &) override { repeated={key,mods}; return true; }
  bool fakeKeyUp(KeyButton button) override {
    released=button; keyEvents.push_back({false,kKeyNone,0,button}); return releaseSucceeds;
  }
  void fakeAllKeysUp() override { ++allReleased; }
  bool fakeCtrlAltDel() override { return false; }
  bool isKeyDown(KeyButton) const override { return false; }
  KeyModifierMask getActiveModifiers() const override { return remainingModifiers; }
  KeyModifierMask pollActiveModifiers() const override { return 0; }
  void pollPressedKeys(KeyButtonSet &) const override {}
protected:
  void updateButtons() override {}
  IKeyState *getKeyState() const override { return nullptr; }
  void handleSystemEvent(const Event &) override {}
};

class ComputerShortcutTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QString::fromLatin1(kRuntimeId)==QString::fromLatin1(kAppId)) QSKIP("Personal-build behavior only.");
    QVERIFY(m_profile.isValid());
    Settings::setSettingsFile(m_profile.filePath("settings.conf"));
    Settings::setStateFile(m_profile.filePath("state.conf"));
  }
  void init()
  {
    auto config=sc::defaults();
    config.apps={"test.app"};
    config.rules=sc::defaultRules(config.apps);
    config.sourceOS=sc::DeviceOS::Windows;
    QVERIFY(sc::save(config));
  }
  void sharedBoundaryMapsAndFreezesUntilRelease()
  {
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,false);
    deskflow::Computer computer(native,&events);
    computer.keyDown('c',KeyModifierControl,10,"");
    QCOMPARE(native->down.modifiers,KeyModifierSuper);
    QCOMPARE(native->appQueries,1);
    native->app="other.app";
    computer.setSourceOperatingSystem(sc::DeviceOS::MacOS);
    computer.keyRepeat('v',0,1,10,"");
    QCOMPARE(native->repeated.key,KeyID('c'));
    QCOMPARE(native->repeated.modifiers,KeyModifierSuper);
    computer.keyUp('c',0,10);
    QCOMPARE(native->released,KeyButton(10));
    computer.keyDown('c',KeyModifierControl,10,"");
    QCOMPARE(native->down.modifiers,KeyModifierControl);
  }
  void primaryInputNeverRunsClientMapping()
  {
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,true);
    deskflow::Computer computer(native,&events);
    computer.keyDown('c',KeyModifierControl,10,"");
    QCOMPARE(native->down.modifiers,KeyModifierControl);
    QCOMPARE(native->appQueries,0);
  }
  void metadataChangesOnlyAffectTheNextPress()
  {
    auto config=sc::load(); sc::setHabit(config,sc::Habit::Automatic); QVERIFY(sc::save(config));
    const bool targetMac=sc::localOS()==sc::DeviceOS::MacOS;
    const auto source=targetMac ? sc::DeviceOS::Windows : sc::DeviceOS::MacOS;
    const auto input=targetMac ? KeyModifierControl : KeyModifierSuper;
    const auto output=targetMac ? KeyModifierSuper : KeyModifierControl;
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,false);
    deskflow::Computer computer(native,&events);
    computer.setSourceOperatingSystem(source);
    computer.keyDown('c',input,10,"");
    QCOMPARE(native->down.modifiers,output);
    computer.setSourceOperatingSystem(sc::localOS());
    computer.keyRepeat('c',input,1,10,"");
    QCOMPARE(native->repeated.modifiers,output);
    computer.keyUp('c',0,10);
    computer.keyDown('c',input,10,"");
    QCOMPARE(native->down.modifiers,input);
  }
  void legacyModifierTranslationPausesOnlyManagedPresets()
  {
    const bool targetMac=sc::localOS()==sc::DeviceOS::MacOS;
    const auto input=targetMac ? KeyModifierControl : KeyModifierSuper;
    const auto output=targetMac ? KeyModifierSuper : KeyModifierControl;
    auto config=sc::load(); sc::setHabit(config,targetMac ? sc::Habit::PC : sc::Habit::Mac);
    config.rules.append({"find","Find",{'f',KeyModifierControl},{'f',KeyModifierSuper},true,config.apps});
    QVERIFY(sc::save(config));
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,false);
    deskflow::Computer computer(native,&events);
    computer.setOptions({kOptionModifierMapForAlt,kKeyModifierIDControl});
    computer.keyDown('c',input,10,"");
    QCOMPARE(native->down.modifiers,input);
    computer.keyUp('c',0,10);
    computer.keyDown('f',KeyModifierControl,11,"");
    QCOMPARE(native->down.modifiers,KeyModifierSuper);
    computer.keyUp('f',0,11);
    computer.resetOptions();
    computer.keyDown('c',input,10,"");
    QCOMPARE(native->down.modifiers,output);
  }
  void leavingReleasesMappedKeysAndClearsTheDecision()
  {
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,false);
    deskflow::Computer computer(native,&events);
    computer.enter(0);
    computer.keyDown('c',KeyModifierControl,10,"");
    QCOMPARE(native->down.modifiers,KeyModifierSuper);
    QVERIFY(computer.leave());
    QCOMPARE(native->allReleased,1);
    native->app="other.app";
    computer.keyDown('c',KeyModifierControl,10,"");
    QCOMPARE(native->down.modifiers,KeyModifierControl);
  }
  void disablingReleasesRemoteKeys_data()
  {
    QTest::addColumn<bool>("entered");
    QTest::newRow("broadcast-without-enter") << false;
    QTest::newRow("leave-refused") << true;
  }
  void disablingReleasesRemoteKeys()
  {
    QFETCH(bool, entered);
    MockEventQueue events;
    auto *native = new RecordingComputer(&events, false);
    deskflow::Computer computer(native, &events);
    computer.enable();
    if (entered) {
      computer.enter(0);
      native->mayLeave = false;
    }
    computer.keyDown('c', KeyModifierControl, 10, "");
    QCOMPARE(native->down.modifiers, KeyModifierSuper);
    computer.disable();
    QCOMPARE(native->allReleased, 1);
    native->app = "other.app";
    computer.keyDown('c', KeyModifierControl, 10, "");
    QCOMPARE(native->down.modifiers, KeyModifierControl);
  }
  void fullShortcutsUseTheSharedBoundaryWithoutBroadProcessQueries()
  {
    const bool targetMac = sc::localOS() == sc::DeviceOS::MacOS;
    const auto input = targetMac ? KeyModifierControl : KeyModifierSuper;
    const auto output = targetMac ? KeyModifierSuper : KeyModifierControl;
    auto config = sc::load();
    sc::setHabit(config, targetMac ? sc::Habit::PC : sc::Habit::Mac);
    config.fullShortcuts = true;
    config.allApplications = true;
    QVERIFY(sc::save(config));
    MockEventQueue events;
    auto *native = new RecordingComputer(&events, false);
    native->app.clear();
    deskflow::Computer computer(native, &events);
    computer.keyDown('A', input | KeyModifierShift, 10, "");
    QCOMPARE(native->down, (sc::Chord{'A', output | KeyModifierShift}));
    QCOMPARE(native->appQueries, 0);
    computer.keyUp('A', 0, 10);
    computer.keyDown(' ', KeyModifierControl, 11, "");
    QCOMPARE(native->down, (sc::Chord{' ', KeyModifierControl}));
    QCOMPARE(native->appQueries, 0);
    computer.keyUp(' ', 0, 11);
    computer.keyDown('c', input, 12, "");
    QCOMPARE(native->appQueries, 1);
    // If an exact scoped C/V rule could apply, an unreadable foreground app
    // cannot accidentally bypass its override with the global fallback.
    QCOMPARE(native->down, (sc::Chord{'c', input}));
    computer.keyUp('c', 0, 12);
    computer.setOptions({kOptionModifierMapForAlt, kKeyModifierIDControl});
    computer.keyDown('a', input, 13, "");
    QCOMPARE(native->down, (sc::Chord{'a', input}));
    computer.keyUp('a', 0, 13);
    computer.resetOptions();
    computer.keyDown('a', input, 13, "");
    QCOMPARE(native->down, (sc::Chord{'a', output}));
  }

  void shiftTapReusesOnlyTheReleasedSlot_data()
  {
    QTest::addColumn<KeyID>("shift");
    QTest::addColumn<KeyButton>("button");
    QTest::newRow("left") << kKeyShift_L << KeyButton(10);
    QTest::newRow("right-last-valid-slot") << kKeyShift_R << KeyButton(511);
  }
  void shiftTapReusesOnlyTheReleasedSlot()
  {
    QFETCH(KeyID, shift); QFETCH(KeyButton, button);
    auto config=sc::load(); config.shiftTapInputMethod=true;
    sc::setHabit(config,sc::Habit::PC); config.fullShortcuts=true; config.allApplications=true;
    QVERIFY(sc::save(config));
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,false);
    sc::ShiftTapGesture::Timestamp now=100;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(shift,KeyModifierShift,button,"");
    now=400; // The 300ms boundary is inclusive.
    computer.keyUp(shift,0,button);
    const bool mac=sc::localOS()==sc::DeviceOS::MacOS;
    QCOMPARE(native->inputSwitches(),mac ? 1 : 0);
    QCOMPARE(native->keyEvents.size(),std::size_t(mac ? 4 : 2));
    QVERIFY(native->keyEvents[0].down);
    QCOMPARE(native->keyEvents[0].key,shift);
    QVERIFY(!native->keyEvents[1].down); // Native Shift is released first.
    QCOMPARE(native->keyEvents[1].button,button);
    if (mac) {
      QCOMPARE(native->keyEvents[2].key,KeyID(' '));
      QCOMPARE(native->keyEvents[2].modifiers,KeyModifierControl);
      QCOMPARE(native->keyEvents[2].button,button);
      QVERIFY(!native->keyEvents[3].down);
      QCOMPARE(native->keyEvents[3].button,button);
    }
    QCOMPARE(native->appQueries,0);
  }

  void shiftTapRequiresRemoteEnabledEnteredAndUntranslatedInput_data()
  {
    QTest::addColumn<QString>("gate");
    for (const auto *name : {"flag-off","global-off","primary","not-entered","not-enabled","server-remap"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void shiftTapRequiresRemoteEnabledEnteredAndUntranslatedInput()
  {
    QFETCH(QString, gate);
    auto config=sc::load(); config.shiftTapInputMethod=gate!="flag-off";
    config.enabled=gate!="global-off"; QVERIFY(sc::save(config));
    MockEventQueue events;
    auto *native=new RecordingComputer(&events,gate=="primary");
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    if (gate!="not-enabled") computer.enable();
    if (gate!="not-entered" && gate!="primary") computer.enter(0);
    if (gate=="server-remap") computer.setOptions({kOptionModifierMapForAlt,kKeyModifierIDControl});
    computer.keyDown(kKeyShift_L,KeyModifierShift,10,""); now=100;
    computer.keyUp(kKeyShift_L,0,10);
    QCOMPARE(native->inputSwitches(),0);
  }

  void shiftTapIsCancelledByRemoteInteractions_data()
  {
    QTest::addColumn<QString>("interaction");
    for (const auto *name : {"key","repeat","mouse","wheel","double-shift"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void shiftTapIsCancelledByRemoteInteractions()
  {
    QFETCH(QString, interaction);
    auto config=sc::load(); config.shiftTapInputMethod=true; QVERIFY(sc::save(config));
    MockEventQueue events; auto *native=new RecordingComputer(&events,false);
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(kKeyShift_L,KeyModifierShift,10,""); now=50;
    if (interaction=="key") computer.keyDown('a',KeyModifierShift,11,"");
    else if (interaction=="repeat") computer.keyRepeat(kKeyShift_L,KeyModifierShift,1,10,"");
    else if (interaction=="mouse") computer.mouseDown(1);
    else if (interaction=="wheel") computer.mouseWheel(0,1);
    else computer.keyDown(kKeyShift_R,KeyModifierShift,12,"");
    now=100; computer.keyUp(kKeyShift_L,0,10);
    QCOMPARE(native->inputSwitches(),0);
  }

  void heldSpaceSurvivesConfigurationMetadataAndOptionReset_data()
  {
    QTest::addColumn<QString>("change");
    for (const auto *name : {"configuration","source-os","options","reset-options"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void heldSpaceSurvivesConfigurationMetadataAndOptionReset()
  {
    QFETCH(QString, change);
    auto config=sc::load(); config.shiftTapInputMethod=true; QVERIFY(sc::save(config));
    MockEventQueue events; auto *native=new RecordingComputer(&events,false);
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(' ',0,20,"");
    if (change=="configuration") { config.allApplications=!config.allApplications; QVERIFY(sc::save(config)); }
    else if (change=="source-os") computer.setSourceOperatingSystem(sc::DeviceOS::Linux);
    else if (change=="options") computer.setOptions({});
    else computer.resetOptions();
    now=20; computer.keyDown(kKeyShift_L,KeyModifierShift,10,"");
    now=100; computer.keyUp(kKeyShift_L,0,10);
    QCOMPARE(native->inputSwitches(),0);
    computer.keyUp(' ',0,20);
    now=200; computer.keyDown(kKeyShift_R,KeyModifierShift,12,"");
    now=250; computer.keyUp(kKeyShift_R,0,12);
    QCOMPARE(native->inputSwitches(),sc::localOS()==sc::DeviceOS::MacOS ? 1 : 0);
  }

  void configurationChangeDuringTapCancelsWithoutAnotherDown()
  {
    auto config=sc::load(); config.shiftTapInputMethod=true; QVERIFY(sc::save(config));
    MockEventQueue events; auto *native=new RecordingComputer(&events,false);
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(kKeyShift_L,KeyModifierShift,10,"");
    config.allApplications=!config.allApplications; QVERIFY(sc::save(config));
    now=100; computer.keyUp(kKeyShift_L,0,10);
    QCOMPARE(native->inputSwitches(),0);
  }

  void shiftTapCannotCrossLifecycleBoundaries_data()
  {
    QTest::addColumn<QString>("boundary");
    for (const auto *name : {"enter","leave-enter","disable-enable","source-os"})
      QTest::newRow(name) << QString::fromLatin1(name);
  }
  void shiftTapCannotCrossLifecycleBoundaries()
  {
    QFETCH(QString, boundary);
    auto config=sc::load(); config.shiftTapInputMethod=true; QVERIFY(sc::save(config));
    MockEventQueue events; auto *native=new RecordingComputer(&events,false);
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(kKeyShift_L,KeyModifierShift,10,"");
    if (boundary=="enter") computer.enter(0);
    else if (boundary=="leave-enter") { QVERIFY(computer.leave()); computer.enter(0); }
    else if (boundary=="disable-enable") { computer.disable(); computer.enable(); computer.enter(0); }
    else computer.setSourceOperatingSystem(sc::DeviceOS::Linux);
    now=100; computer.keyUp(kKeyShift_L,0,10);
    QCOMPARE(native->inputSwitches(),0);
  }

  void failedReleaseInvalidSlotAndResidualModifiersPreventSynthesis_data()
  {
    QTest::addColumn<KeyButton>("button");
    QTest::addColumn<bool>("released");
    QTest::addColumn<KeyModifierMask>("modifiers");
    QTest::newRow("failed-release") << KeyButton(10) << false << KeyModifierMask(0);
    QTest::newRow("zero-slot") << KeyButton(0) << true << KeyModifierMask(0);
    QTest::newRow("out-of-range-slot") << KeyButton(512) << true << KeyModifierMask(0);
    QTest::newRow("shift-still-held") << KeyButton(10) << true << KeyModifierShift;
    QTest::newRow("control-still-held") << KeyButton(10) << true << KeyModifierControl;
  }
  void failedReleaseInvalidSlotAndResidualModifiersPreventSynthesis()
  {
    QFETCH(KeyButton, button); QFETCH(bool, released); QFETCH(KeyModifierMask, modifiers);
    auto config=sc::load(); config.shiftTapInputMethod=true; QVERIFY(sc::save(config));
    MockEventQueue events; auto *native=new RecordingComputer(&events,false);
    native->releaseSucceeds=released; native->remainingModifiers=modifiers;
    sc::ShiftTapGesture::Timestamp now=0;
    deskflow::Computer computer(native,&events,[&] { return now; });
    computer.enable(); computer.enter(0);
    computer.keyDown(kKeyShift_L,KeyModifierShift,button,""); now=100;
    computer.keyUp(kKeyShift_L,0,button);
    QCOMPARE(native->inputSwitches(),0);
  }
private:
  Log m_log;
  QTemporaryDir m_profile;
};
QTEST_GUILESS_MAIN(ComputerShortcutTests)
#include "ComputerShortcutTests.moc"
