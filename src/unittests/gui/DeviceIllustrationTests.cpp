/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/Settings.h"
#include "gui/personal/DeviceIllustration.h"
#include "gui/personal/LoginStartup.h"
#include "gui/personal/PersonalWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPalette>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>
#include <utility>

using deskflow::gui::personal::DeviceIllustration;
using deskflow::gui::personal::LoginStartup;
using deskflow::gui::personal::PersonalWindow;
using deskflow::layout::Direction;
using deskflow::layout::Snapshot;

namespace {
Snapshot snapshot(Direction direction = Direction::Right)
{
  return {QStringLiteral("ubuntu-source"), QStringLiteral("my-mac"), direction, true, {}};
}

Snapshot topology(const QList<deskflow::layout::Screen> &screens)
{
  Snapshot result;
  result.serverName = QStringLiteral("source");
  result.clientName = QStringLiteral("client-a");
  result.editable = true;
  result.screens = screens;
  result.localName = QStringLiteral("client-a");
  result.protocolVersion = 2;
  result.connectedNames = {QStringLiteral("source"), QStringLiteral("client-a")};
  result.revision = deskflow::layout::revisionFor(result);
  return result;
}

Snapshot threeScreens()
{
  return topology(
      {{QStringLiteral("source"), 0, 0}, {QStringLiteral("client-a"), 1, 0}, {QStringLiteral("client-b"), 0, 1}}
  );
}

QPoint cellPoint(const DeviceIllustration &widget, const Snapshot &state, int column, int row)
{
  const auto &reference = state.screens.first();
  const auto rectangle = widget.screenGeometry(reference.name);
  const qreal scale = rectangle.width() / 132;
  return (rectangle.center() + QPointF((column - reference.column) * 150 * scale, (row - reference.row) * 96 * scale))
      .toPoint();
}

QPoint target(Direction direction)
{
  switch (direction) {
  case Direction::Left:
    return {105, 140};
  case Direction::Right:
    return {495, 140};
  case Direction::Above:
    return {300, 43};
  case Direction::Below:
    return {300, 237};
  default:
    return {300, 140};
  }
}

void mouse(DeviceIllustration &widget, QEvent::Type type, const QPoint &point)
{
  const bool release = type == QEvent::MouseButtonRelease;
  QMouseEvent event(
      type, point, widget.mapToGlobal(point), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
      release ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier
  );
  QCoreApplication::sendEvent(&widget, &event);
}

void drag(DeviceIllustration &widget, const QPoint &from, const QPoint &to)
{
  mouse(widget, QEvent::MouseButtonPress, from);
  mouse(widget, QEvent::MouseMove, to);
  mouse(widget, QEvent::MouseButtonRelease, to);
}

void prepare(DeviceIllustration &widget, const Snapshot &state)
{
  widget.resize(600, 280);
  widget.setScreenLayout(state);
  widget.show();
  QCoreApplication::processEvents();
}
} // namespace

class DeviceIllustrationTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Widget regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
    QVERIFY(m_profile.isValid());
    qputenv("XDG_CONFIG_HOME", m_profile.path().toUtf8());
    qputenv("XDG_STATE_HOME", m_profile.path().toUtf8());
    Settings::setSettingsFile(m_profile.filePath("settings.conf"));
    Settings::setStateFile(m_profile.filePath("state.conf"));
  }

  void draggingClientRequestsAllFourDirections_data()
  {
    QTest::addColumn<Direction>("direction");
    QTest::newRow("left") << Direction::Left;
    QTest::newRow("right") << Direction::Right;
    QTest::newRow("above") << Direction::Above;
    QTest::newRow("below") << Direction::Below;
  }

  void draggingClientRequestsAllFourDirections()
  {
    QFETCH(Direction, direction);
    const auto actual = snapshot(deskflow::layout::opposite(direction));
    DeviceIllustration widget;
    prepare(widget, actual);
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    drag(widget, target(actual.direction), target(direction));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(qvariant_cast<Direction>(changes.first().first()), direction);
    // A request is not an acknowledgement: the drawing remains authoritative.
    QCOMPARE(widget.screenLayout().direction, actual.direction);
    widget.setScreenLayout(snapshot(direction));
    QCOMPARE(widget.screenLayout().direction, direction);
    QVERIFY(widget.accessibleName().contains(QStringLiteral("my-mac")));
  }

  void draggingSourceInvertsItsMovement_data()
  {
    draggingClientRequestsAllFourDirections_data();
  }

  void draggingSourceInvertsItsMovement()
  {
    QFETCH(Direction, direction);
    const auto actual = snapshot(deskflow::layout::opposite(direction));
    DeviceIllustration widget;
    prepare(widget, actual);
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    drag(widget, {300, 140}, target(deskflow::layout::opposite(direction)));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(qvariant_cast<Direction>(changes.first().first()), direction);
    QCOMPARE(widget.screenLayout().direction, actual.direction);
  }

  void keyboardRequestsEveryDirection()
  {
    DeviceIllustration widget;
    prepare(widget, snapshot());
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    const QList<std::pair<Qt::Key, Direction>> keys{
        {Qt::Key_Left, Direction::Left},
        {Qt::Key_Up, Direction::Above},
        {Qt::Key_Down, Direction::Below},
        {Qt::Key_Right, Direction::Right}
    };
    for (const auto &[key, direction] : keys) {
      widget.setScreenLayout(snapshot(deskflow::layout::opposite(direction)));
      widget.setFocus();
      QTest::keyClick(&widget, key);
      QCOMPARE(changes.count(), 1);
      QCOMPARE(qvariant_cast<Direction>(changes.takeFirst().first()), direction);
    }
    widget.setScreenLayout(snapshot(Direction::Left));
    QTest::keyClick(&widget, Qt::Key_Left);
    QTest::keyClick(&widget, Qt::Key_Right, Qt::ControlModifier);
    QCOMPARE(changes.count(), 0);
  }

  void cancelAndInvalidDropsDoNotWrite()
  {
    DeviceIllustration widget;
    prepare(widget, snapshot());
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    mouse(widget, QEvent::MouseButtonPress, target(Direction::Right));
    mouse(widget, QEvent::MouseMove, target(Direction::Above));
    QTest::keyClick(&widget, Qt::Key_Escape);
    mouse(widget, QEvent::MouseButtonRelease, target(Direction::Above));
    drag(widget, target(Direction::Right), {-20, -20});
    drag(widget, target(Direction::Right), {10, 10});
    drag(widget, target(Direction::Right), {300, 140});
    drag(widget, {10, 10}, target(Direction::Left));
    mouse(widget, QEvent::MouseButtonPress, target(Direction::Right));
    mouse(widget, QEvent::MouseButtonRelease, target(Direction::Right));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(widget.screenLayout().direction, Direction::Right);
  }

  void pendingBusyReadOnlyAndUnknownCannotWrite()
  {
    DeviceIllustration widget;
    prepare(widget, snapshot());
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    const auto attempt = [&] {
      drag(widget, target(Direction::Right), target(Direction::Above));
      QTest::keyClick(&widget, Qt::Key_Left);
      QCOMPARE(changes.count(), 0);
    };
    widget.setScreenLayout(snapshot(), true);
    QVERIFY(widget.accessibleDescription().contains(QStringLiteral("正在同步")));
    attempt();
    widget.setScreenLayout(snapshot());
    widget.setConfigurationBusy(true);
    attempt();
    widget.setConfigurationBusy(false);
    auto readOnly = snapshot();
    readOnly.editable = false;
    readOnly.error = QStringLiteral("来源电脑不支持远程布局");
    widget.setScreenLayout(readOnly);
    QCOMPARE(widget.accessibleDescription(), readOnly.error);
    attempt();
    auto unknown = snapshot(Direction::Unknown);
    widget.setScreenLayout(unknown);
    QVERIFY(widget.accessibleName().contains(QStringLiteral("尚未确认")));
    attempt();
    widget.setScreenLayout(snapshot());
    widget.setEnabled(false);
    attempt();
    widget.setEnabled(true);
    QTest::keyClick(&widget, Qt::Key_Up);
    QCOMPARE(changes.count(), 1);
  }

  void authorityRefreshCancelsAnInProgressDrag()
  {
    DeviceIllustration widget;
    prepare(widget, snapshot());
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    mouse(widget, QEvent::MouseButtonPress, target(Direction::Right));
    mouse(widget, QEvent::MouseMove, target(Direction::Above));
    widget.setScreenLayout(snapshot(Direction::Below));
    mouse(widget, QEvent::MouseButtonRelease, target(Direction::Above));
    QCOMPARE(changes.count(), 0);
    QCOMPARE(widget.screenLayout().direction, Direction::Below);
  }

  void unchangedAuthorityRefreshPreservesAnInProgressDrag()
  {
    DeviceIllustration widget;
    prepare(widget, snapshot());
    QSignalSpy changes(&widget, &DeviceIllustration::screenLayoutRequested);
    mouse(widget, QEvent::MouseButtonPress, target(Direction::Right));
    mouse(widget, QEvent::MouseMove, target(Direction::Above));
    widget.setConfigurationBusy(false);
    widget.setScreenLayout(snapshot());
    mouse(widget, QEvent::MouseButtonRelease, target(Direction::Above));
    QCOMPARE(changes.count(), 1);
    QCOMPARE(qvariant_cast<Direction>(changes.first().first()), Direction::Above);
  }

  void topologyDragsMoveSourceAndClientsWithoutOptimisticChanges_data()
  {
    QTest::addColumn<QString>("name");
    QTest::newRow("source-reanchor") << QStringLiteral("source");
    QTest::newRow("client") << QStringLiteral("client-a");
  }

  void topologyDragsMoveSourceAndClientsWithoutOptimisticChanges()
  {
    QFETCH(QString, name);
    const auto actual = threeScreens();
    DeviceIllustration widget;
    prepare(widget, actual);
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    QSignalSpy legacy(&widget, &DeviceIllustration::screenLayoutRequested);
    drag(widget, widget.screenGeometry(name).center().toPoint(), cellPoint(widget, actual, 1, 1));
    QCOMPARE(moves.count(), 1);
    QCOMPARE(legacy.count(), 0);
    const auto move = qvariant_cast<deskflow::layout::Move>(moves.first().first());
    QCOMPARE(move.screenName, name);
    QCOMPARE(move.column, 1);
    QCOMPARE(move.row, 1);
    QCOMPARE(move.expectedRevision, actual.revision);
    QCOMPARE(widget.screenLayout().screens, actual.screens);
    auto applied = deskflow::layout::movedSnapshot(actual, move);
    QVERIFY(applied.has_value());
    widget.setScreenLayout(*applied);
    QCOMPARE(widget.screenLayout().screens, applied->screens);
  }

  void fourComputerLAndTLayoutsCanBeRearranged()
  {
    DeviceIllustration widget;
    const auto ell = topology(
        {{QStringLiteral("source"), 0, 0},
         {QStringLiteral("client-a"), 1, 0},
         {QStringLiteral("end"), 2, 0},
         {QStringLiteral("bottom"), 0, 1}}
    );
    prepare(widget, ell);
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    drag(widget, widget.screenGeometry(QStringLiteral("end")).center().toPoint(), cellPoint(widget, ell, 1, 1));
    QCOMPARE(moves.count(), 1);
    auto move = qvariant_cast<deskflow::layout::Move>(moves.takeFirst().first());
    QCOMPARE(move.screenName, QStringLiteral("end"));
    QCOMPARE(move.column, 1);
    QCOMPARE(move.row, 1);
    const auto tee = topology(
        {{QStringLiteral("source"), 0, 0},
         {QStringLiteral("left"), -1, 0},
         {QStringLiteral("client-a"), 1, 0},
         {QStringLiteral("bottom"), 0, 1}}
    );
    widget.resize(600, 400);
    widget.setScreenLayout(tee);
    widget.findChild<QToolButton *>(QStringLiteral("screenLayoutZoomOut"))->click();
    drag(widget, widget.screenGeometry(QStringLiteral("bottom")).center().toPoint(), cellPoint(widget, tee, 0, -1));
    QCOMPARE(moves.count(), 1);
    move = qvariant_cast<deskflow::layout::Move>(moves.first().first());
    QCOMPARE(move.screenName, QStringLiteral("bottom"));
    QCOMPARE(move.column, 0);
    QCOMPARE(move.row, -1);
    QCOMPARE(widget.screenLayout().screens, tee.screens);
  }

  void edgeDropKeepsTheSnappedCellAfterAutomaticPan()
  {
    const auto tee = topology(
        {{QStringLiteral("source"), 0, 0},
         {QStringLiteral("left"), -1, 0},
         {QStringLiteral("client-a"), 1, 0},
         {QStringLiteral("bottom"), 0, 1}}
    );
    DeviceIllustration widget;
    prepare(widget, tee);
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    const QPoint edge(qRound(widget.screenGeometry(QStringLiteral("source")).center().x()), 42);
    drag(widget, widget.screenGeometry(QStringLiteral("bottom")).center().toPoint(), edge);
    QCOMPARE(moves.count(), 1);
    const auto move = qvariant_cast<deskflow::layout::Move>(moves.first().first());
    QCOMPARE(move.column, 0);
    QCOMPARE(move.row, -1);
    QCOMPARE(widget.screenLayout().screens, tee.screens);
  }

  void topologyPairKeyboardStillRotatesToEveryCardinalSide()
  {
    const auto pair = topology({{QStringLiteral("source"), 0, 0}, {QStringLiteral("client-a"), 1, 0}});
    DeviceIllustration widget;
    prepare(widget, pair);
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    QTest::keyClick(&widget, Qt::Key_Up);
    QCOMPARE(moves.count(), 1);
    auto move = qvariant_cast<deskflow::layout::Move>(moves.takeFirst().first());
    QCOMPARE(move.screenName, QStringLiteral("client-a"));
    QCOMPARE(move.column, 0);
    QCOMPARE(move.row, -1);
    widget.findChild<QComboBox *>(QStringLiteral("screenLayoutDevicePicker"))->setCurrentText(QStringLiteral("source"));
    QTest::keyClick(&widget, Qt::Key_Down);
    QCOMPARE(moves.count(), 1);
    move = qvariant_cast<deskflow::layout::Move>(moves.first().first());
    QCOMPARE(move.screenName, QStringLiteral("source"));
    QCOMPARE(move.column, 1);
    QCOMPARE(move.row, 1);
  }

  void topologyCollisionsAndBridgeDisconnectsAreRejected()
  {
    DeviceIllustration widget;
    const auto actual = threeScreens();
    prepare(widget, actual);
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    drag(
        widget, widget.screenGeometry(QStringLiteral("client-a")).center().toPoint(),
        widget.screenGeometry(QStringLiteral("client-b")).center().toPoint()
    );
    QCOMPARE(moves.count(), 0);
    QVERIFY(!widget.accessibleDescription().isEmpty());
    const auto tee = topology(
        {{QStringLiteral("source"), 0, 0},
         {QStringLiteral("left"), -1, 0},
         {QStringLiteral("client-a"), 1, 0},
         {QStringLiteral("bottom"), 0, 1}}
    );
    widget.setScreenLayout(tee);
    auto *picker = widget.findChild<QComboBox *>(QStringLiteral("screenLayoutDevicePicker"));
    QVERIFY(picker);
    picker->setCurrentText(QStringLiteral("source"));
    QTest::keyClick(&widget, Qt::Key_Up);
    QCOMPARE(moves.count(), 0);
    QCOMPARE(widget.screenLayout().screens, tee.screens);
  }

  void topologyKeyboardSelectionPendingAndCancellation()
  {
    DeviceIllustration widget;
    const auto actual = threeScreens();
    prepare(widget, actual);
    auto *picker = widget.findChild<QComboBox *>(QStringLiteral("screenLayoutDevicePicker"));
    QVERIFY(picker);
    picker->setCurrentText(QStringLiteral("client-b"));
    QVERIFY(widget.accessibleName().contains(QStringLiteral("client-b")));
    QVERIFY(widget.accessibleName().contains(QStringLiteral("离线")));
    picker->setCurrentText(QStringLiteral("client-a"));
    QSignalSpy moves(&widget, &DeviceIllustration::screenMoveRequested);
    QTest::keyClick(&widget, Qt::Key_Down);
    QCOMPARE(moves.count(), 1);
    QCOMPARE(qvariant_cast<deskflow::layout::Move>(moves.takeFirst().first()).screenName, QStringLiteral("client-a"));
    const auto from = widget.screenGeometry(QStringLiteral("client-a")).center().toPoint();
    const auto to = cellPoint(widget, actual, 1, 1);
    mouse(widget, QEvent::MouseButtonPress, from);
    mouse(widget, QEvent::MouseMove, to);
    QTest::keyClick(&widget, Qt::Key_Escape);
    mouse(widget, QEvent::MouseButtonRelease, to);
    drag(widget, from, {-40, -40});
    mouse(widget, QEvent::MouseButtonPress, from);
    mouse(widget, QEvent::MouseMove, from + QPoint(15, 0));
    mouse(widget, QEvent::MouseButtonRelease, from);
    QCOMPARE(moves.count(), 0);
    widget.setScreenLayout(actual, true);
    QTest::keyClick(&widget, Qt::Key_Down);
    drag(widget, from, to);
    QCOMPARE(moves.count(), 0);
    QCOMPARE(widget.screenLayout().screens, actual.screens);
  }

  void fifteenScreensFitAndViewControlsPreserveTheActualGraph()
  {
    QList<deskflow::layout::Screen> screens;
    for (int row = 0; row < 3; ++row)
      for (int column = 0; column < 5; ++column)
        screens.append(
            {row == 0 && column == 0   ? QStringLiteral("source")
             : row == 0 && column == 1 ? QStringLiteral("client-a")
                                       : QStringLiteral("computer-%1").arg(row * 5 + column),
             column, row}
        );
    const auto actual = topology(screens);
    DeviceIllustration widget;
    prepare(widget, actual);
    const QRectF area(4, 41, widget.width() - 8, widget.height() - 65);
    for (const auto &screen : screens)
      QVERIFY(area.contains(widget.screenGeometry(screen.name)));
    const auto initial = widget.screenGeometry(QStringLiteral("source"));
    auto *zoom = widget.findChild<QToolButton *>(QStringLiteral("screenLayoutZoomIn"));
    auto *fit = widget.findChild<QToolButton *>(QStringLiteral("screenLayoutFit"));
    QVERIFY(zoom && fit);
    zoom->click();
    QVERIFY(widget.screenGeometry(QStringLiteral("source")).width() > initial.width());
    fit->click();
    QCOMPARE(widget.screenGeometry(QStringLiteral("source")), initial);
    QCOMPARE(widget.screenLayout().screens, actual.screens);
  }

  void connectionRowsKeepTheirTextVisibleAfterStylePolish()
  {
    LoginStartup startup(
        {[] { return LoginStartup::Status{}; }, [](bool) { return LoginStartup::ChangeResult{}; }, [] {}}
    );
    PersonalWindow window(nullptr, &startup);
    window.show();
    for (const QSize size : {QSize(900, 620), QSize(1060, 720)}) {
      window.resize(size);
      // Each page change re-polishes the shared stylesheet. The generic
      // button minimum must not shrink content rows and clip every label.
      window.showPage(PersonalWindow::General);
      window.showPage(PersonalWindow::Connection);
      QCoreApplication::processEvents();
      int rows = 0;
      for (auto *button : window.findChildren<QPushButton *>()) {
        if (!button->property("personalRow").toBool())
          continue;
        ++rows;
        QVERIFY2(button->height() >= 76, qPrintable(button->accessibleName()));
        for (auto *text : button->findChildren<QLabel *>()) {
          QVERIFY2(text->height() >= text->fontMetrics().height(), qPrintable(text->text()));
          QVERIFY2(button->rect().contains(text->geometry()), qPrintable(text->text()));
        }
      }
      QCOMPARE(rows, 1);
    }
  }

  void renderLayoutScreenshots()
  {
    const QString directory = qEnvironmentVariable("DESKBRIDGE_LAYOUT_SCREENSHOTS");
    if (directory.isEmpty())
      QSKIP("Set DESKBRIDGE_LAYOUT_SCREENSHOTS to render visual QA fixtures.");
    QVERIFY(QDir().mkpath(directory));
    const QPalette original = QApplication::palette();
    // Restore global appearance even if an image assertion fails.
    struct RestorePalette
    {
      QPalette palette;
      ~RestorePalette()
      {
        QApplication::setPalette(palette);
      }
    } restore{original};
    for (const bool dark : {false, true}) {
      QPalette palette = original;
      if (dark) {
        palette.setColor(QPalette::Window, QColor("#202024"));
        palette.setColor(QPalette::WindowText, QColor("#f2f2f5"));
        palette.setColor(QPalette::Base, QColor("#29292e"));
        palette.setColor(QPalette::Text, QColor("#f2f2f5"));
        palette.setColor(QPalette::Button, QColor("#303036"));
        palette.setColor(QPalette::ButtonText, QColor("#f2f2f5"));
        palette.setColor(QPalette::PlaceholderText, QColor("#a9a9b4"));
      } else {
        palette.setColor(QPalette::Window, QColor("#f7f7fa"));
        palette.setColor(QPalette::WindowText, QColor("#25252a"));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::Text, QColor("#25252a"));
        palette.setColor(QPalette::PlaceholderText, QColor("#74747d"));
      }
      QApplication::setPalette(palette);
      LoginStartup startup(
          {[] { return LoginStartup::Status{false, false, false, {}, {}}; },
           [](bool) { return LoginStartup::ChangeResult{}; }, [] {}}
      );
      PersonalWindow window(nullptr, &startup);
      window.setDeviceContext(deskflow::shortcuts::DeviceOS::Linux, false);
      window.setConnectionStatus(
          deskflow::core::ConnectionState::Connected, deskflow::core::ProcessState::Started, true, true, true,
          QStringLiteral("192.0.2.10"), QStringLiteral("my-mac")
      );
      window.setMappingStatus(true, 4, QStringLiteral("完整快捷键适配 · 全部应用 · Ctrl+空格保留"));
      const QList<std::pair<Direction, QString>> choices{
          {Direction::Left, QStringLiteral("left")},
          {Direction::Right, QStringLiteral("right")},
          {Direction::Above, QStringLiteral("above")},
          {Direction::Below, QStringLiteral("below")}
      };
      for (const QSize size : {QSize(1060, 720), QSize(900, 620)}) {
        window.resize(size);
        window.show();
        QCoreApplication::processEvents();
        for (const auto &[direction, name] : choices) {
          window.setScreenLayout(snapshot(direction));
          window.findChild<DeviceIllustration *>()->clearFocus();
          QCoreApplication::processEvents();
          const auto filename = QStringLiteral("%1/%2-%3-%4x%5.png")
                                    .arg(directory, dark ? QStringLiteral("dark") : QStringLiteral("light"), name)
                                    .arg(size.width())
                                    .arg(size.height());
          QVERIFY2(window.grab().save(filename), qPrintable(filename));
        }
      }
      QList<deskflow::layout::Screen> fifteen;
      for (int row = 0; row < 3; ++row)
        for (int column = 0; column < 5; ++column)
          fifteen.append(
              {row == 0 && column == 0   ? QStringLiteral("source")
               : row == 0 && column == 1 ? QStringLiteral("client-a")
                                         : QStringLiteral("computer-%1").arg(row * 5 + column),
               column, row}
          );
      const QList<std::pair<QString, Snapshot>> graphs{
          {QStringLiteral("three-L"), threeScreens()},
          {QStringLiteral("four-T"), topology(
                                         {{QStringLiteral("source"), 0, 0},
                                          {QStringLiteral("left"), -1, 0},
                                          {QStringLiteral("client-a"), 1, 0},
                                          {QStringLiteral("bottom"), 0, 1}}
                                     )},
          {QStringLiteral("fifteen"), topology(fifteen)}
      };
      for (const QSize size : {QSize(1060, 720), QSize(900, 620)}) {
        window.resize(size);
        for (const auto &[name, graph] : graphs) {
          window.setScreenLayout(graph);
          QCoreApplication::processEvents();
          const auto filename = QStringLiteral("%1/%2-%3-%4x%5.png")
                                    .arg(directory, dark ? QStringLiteral("dark") : QStringLiteral("light"), name)
                                    .arg(size.width())
                                    .arg(size.height());
          QVERIFY(window.grab().save(filename));
        }
      }
      window.resize(900, 620);
      for (const Direction direction : {Direction::Above, Direction::Below}) {
        window.setScreenLayout(snapshot(direction));
        window.findChild<DeviceIllustration *>()->setFocus();
        QCoreApplication::processEvents();
        const auto file = QStringLiteral("%1/%2-focused-%3.png")
                              .arg(
                                  directory, dark ? QStringLiteral("dark") : QStringLiteral("light"),
                                  direction == Direction::Above ? QStringLiteral("above") : QStringLiteral("below")
                              );
        QVERIFY(window.grab().save(file));
      }
      window.setScreenLayout(snapshot(Direction::Right));
      auto *widget = window.findChild<DeviceIllustration *>();
      widget->setFocus();
      QCoreApplication::processEvents();
      const auto map = [widget](const QPoint &point) {
        const qreal scale = qMin(widget->width() / 600.0, widget->height() / 280.0);
        return (QPointF((widget->width() - 600 * scale) / 2, (widget->height() - 280 * scale) / 2) +
                QPointF(point) * scale)
            .toPoint();
      };
      mouse(*widget, QEvent::MouseButtonPress, map(target(Direction::Right)));
      mouse(*widget, QEvent::MouseMove, map(target(Direction::Above)));
      QCoreApplication::processEvents();
      QVERIFY(
          window.grab().save(directory + (dark ? QStringLiteral("/dark-drag.png") : QStringLiteral("/light-drag.png")))
      );
      QTest::keyClick(widget, Qt::Key_Escape);
      mouse(*widget, QEvent::MouseButtonPress, map({300, 140}));
      mouse(*widget, QEvent::MouseMove, map(target(Direction::Above)));
      QCoreApplication::processEvents();
      QVERIFY(window.grab().save(
          directory + (dark ? QStringLiteral("/dark-source-drag.png") : QStringLiteral("/light-source-drag.png"))
      ));
      QTest::keyClick(widget, Qt::Key_Escape);
      window.setScreenLayout(snapshot(Direction::Right), true);
      QCoreApplication::processEvents();
      QVERIFY(window.grab().save(
          directory + (dark ? QStringLiteral("/dark-pending.png") : QStringLiteral("/light-pending.png"))
      ));
      window.setScreenLayout(
          Snapshot{{}, {}, Direction::Unknown, false, QStringLiteral("正在等待来源电脑提供实际屏幕布局")}
      );
      QCoreApplication::processEvents();
      QVERIFY(window.grab().save(
          directory + (dark ? QStringLiteral("/dark-unknown.png") : QStringLiteral("/light-unknown.png"))
      ));
    }
  }

  void uncertainSynchronizationPermitsOnlyStoppingAnExistingConnection()
  {
    LoginStartup startup(
        {[] { return LoginStartup::Status{true, false, false, {}, {}}; },
         [](bool) { return LoginStartup::ChangeResult{}; }, [] {}}
    );
    PersonalWindow window(nullptr, &startup);
    auto *primary = window.findChild<QPushButton *>(QStringLiteral("personalPrimaryConnection"));
    auto *automatic = window.findChild<QCheckBox *>(QStringLiteral("automaticSharingSwitch"));
    auto *apply = window.findChild<QPushButton *>(QStringLiteral("applyGeneralPreferences"));
    auto *note = window.findChild<QLabel *>(QStringLiteral("personalLayoutNote"));
    auto *devices = window.findChild<DeviceIllustration *>();
    QVERIFY(primary && automatic && apply && note && devices);
    QPushButton *options = nullptr;
    for (auto *button : window.findChildren<QPushButton *>())
      if (button->accessibleName() == QStringLiteral("连接设置"))
        options = button;
    QVERIFY(options);
    const bool savedDraft = !automatic->isChecked();
    automatic->setChecked(savedDraft);
    QSignalSpy starts(&window, &PersonalWindow::connectRequested);
    QSignalSpy stops(&window, &PersonalWindow::disconnectRequested);
    QSignalSpy preferences(&window, &PersonalWindow::preferencesApplyRequested);
    QSignalSpy layouts(&window, &PersonalWindow::screenLayoutRequested);
    const QString feedback = QStringLiteral("暂时无法确认来源端状态，请停止共享后重试。");
    window.setDeviceContext(deskflow::shortcuts::DeviceOS::Linux, true);
    window.setScreenLayout(snapshot(), true, feedback);
    window.setConfigurationBusy(true, true);
    window.setConnectionStatus(
        deskflow::core::ConnectionState::Listening, deskflow::core::ProcessState::Started, true, false, true, {}, {}
    );
    QCOMPARE(note->text(), feedback);
    QCOMPARE(devices->accessibleDescription(), feedback);
    QVERIFY(primary->isEnabled());
    QVERIFY(!options->isEnabled() && !automatic->isEnabled() && !apply->isEnabled());
    window.reloadPreferences();
    QVERIFY(!automatic->isEnabled());
    QCOMPARE(automatic->isChecked(), savedDraft);
    primary->click();
    QCOMPARE(stops.count(), 1);
    QCOMPARE(starts.count(), 0);
    apply->click();
    QTest::keyClick(devices, Qt::Key_Up);
    QCOMPARE(preferences.count(), 0);
    QCOMPARE(layouts.count(), 0);
    window.setConfigurationBusy(true);
    QVERIFY(!primary->isEnabled());
    window.setConfigurationBusy(true, true);
    window.setConnectionStatus(
        deskflow::core::ConnectionState::Disconnected, deskflow::core::ProcessState::Stopping, true, false, true, {}, {}
    );
    QVERIFY(!primary->isEnabled());
    window.setConnectionStatus(
        deskflow::core::ConnectionState::Disconnected, deskflow::core::ProcessState::Stopped, true, false, true, {}, {}
    );
    QVERIFY(!primary->isEnabled());
    primary->click();
    QCOMPARE(starts.count(), 0);
    QCOMPARE(stops.count(), 1);
    window.setConfigurationBusy(false);
    QVERIFY(primary->isEnabled() && options->isEnabled() && automatic->isEnabled());
    QCOMPARE(automatic->isChecked(), savedDraft);
  }

  void personalWindowForwardsRequestsAndDescribesConfirmedEdges()
  {
    LoginStartup startup(
        {[] { return LoginStartup::Status{false, false, false, {}, {}}; },
         [](bool) { return LoginStartup::ChangeResult{}; }, [] {}}
    );
    PersonalWindow window(nullptr, &startup);
    auto *widget = window.findChild<DeviceIllustration *>();
    auto *hint = window.findChild<QLabel *>(QStringLiteral("personalConnectionHint"));
    auto *note = window.findChild<QLabel *>(QStringLiteral("personalLayoutNote"));
    QVERIFY(widget && hint && note);
    auto *status = window.findChild<QLabel *>(QStringLiteral("personalConnectionStatus"));
    auto *security = window.findChild<QLabel *>(QStringLiteral("personalConnectionSecurity"));
    QVERIFY(status && security);
    QVERIFY(!status->wordWrap() && !security->wordWrap());
    QSignalSpy changes(&window, &PersonalWindow::screenLayoutRequested);
    window.setScreenLayout(snapshot(Direction::Above));
    QVERIFY(hint->text().contains(QStringLiteral("来源的上边缘")));
    QVERIFY(hint->text().contains(QStringLiteral("电脑的下边缘")));
    QTest::keyClick(widget, Qt::Key_Left);
    QCOMPARE(changes.count(), 1);
    QVERIFY(hint->text().contains(QStringLiteral("来源的上边缘")));
    window.setScreenLayout(snapshot(Direction::Above), true);
    QVERIFY(note->text().contains(QStringLiteral("正在同步")));
    QTest::keyClick(widget, Qt::Key_Down);
    QCOMPARE(changes.count(), 1);
    window.setScreenLayout(snapshot(Direction::Left), false, QStringLiteral("布局已同步"));
    QVERIFY(hint->text().contains(QStringLiteral("来源的左边缘")));
    QCOMPARE(note->text(), QStringLiteral("布局已同步"));
    window.setConfigurationBusy(true);
    QTest::keyClick(widget, Qt::Key_Right);
    QCOMPARE(changes.count(), 1);
  }

private:
  QTemporaryDir m_profile;
};

QTEST_MAIN(DeviceIllustrationTests)
#include "DeviceIllustrationTests.moc"
