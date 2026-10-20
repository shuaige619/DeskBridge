/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#include "common/ShortcutRules.h"
#include "gui/personal/ApplicationPicker.h"
#include "gui/personal/RuleEditor.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QtTest>

using namespace deskflow::gui::personal;

class PersonalDialogTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase()
  {
    if (QGuiApplication::platformName() != QStringLiteral("offscreen") &&
        QGuiApplication::platformName() != QStringLiteral("minimal"))
      QSKIP("Widget regression tests require QT_QPA_PLATFORM=offscreen or minimal.");
  }

  void ruleEditorKeepsKeyboardCapableDialogType()
  {
    QWidget parent;
    const auto configuration = deskflow::shortcuts::defaults();
    RuleEditor editor(configuration, configuration.rules.first(), &parent);
    // On Cocoa, WindowModal already presents a QDialog as a native sheet.
    // OR-ing Sheet into Dialog produces Drawer, whose QNSPanel refuses keys.
    QCOMPARE(editor.windowType(), Qt::Dialog);
    QCOMPARE(editor.windowModality(), Qt::WindowModal);
  }

  void applicationPickerKeepsKeyboardCapableDialogType()
  {
    QWidget parent;
    ApplicationPicker picker({}, catalog(), &parent);
    QCOMPARE(picker.windowType(), Qt::Dialog);
    QCOMPARE(picker.windowModality(), Qt::WindowModal);
  }

  void clickingAnApplicationRowSelectsAndClickingAgainCancels()
  {
    ApplicationPicker picker({}, catalog());
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("applicationChoices"));
    auto *confirm = picker.findChild<QPushButton *>(QStringLiteral("confirmApplications"));
    auto *summary = picker.findChild<QLabel *>(QStringLiteral("applicationSelectionSummary"));
    QVERIFY(list && confirm && summary);
    QCOMPARE(list->selectionMode(), QAbstractItemView::MultiSelection);
    QVERIFY(!confirm->isEnabled());
    auto *alpha = application(list, QStringLiteral("fixture.alpha"));
    QVERIFY(alpha);
    QVERIFY(!(alpha->flags() & Qt::ItemIsUserCheckable));
    QVERIFY(!alpha->data(Qt::CheckStateRole).isValid());
    picker.show();
    QApplication::processEvents();
    const auto row = list->visualItemRect(alpha);
    QVERIFY(row.isValid());
    const auto namePosition = QPoint(row.left() + list->iconSize().width() + 16, row.center().y());
    // Ordinary clicks in the name/row area use exactly the state returned to callers.
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, namePosition);
    QVERIFY(alpha->isSelected());
    QCOMPARE(picker.selectedApplications(), QStringList{QStringLiteral("fixture.alpha")});
    QVERIFY(confirm->isEnabled());
    QCOMPARE(confirm->text(), QStringLiteral("添加 1 个应用"));
    QVERIFY(summary->text().contains(QStringLiteral("1")));
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, namePosition);
    QVERIFY(!alpha->isSelected());
    QVERIFY(picker.selectedApplications().isEmpty());
    QVERIFY(!confirm->isEnabled());
  }

  void multiSelectionSurvivesSearchAndAccessibleSelectionChanges()
  {
    ApplicationPicker picker({}, catalog());
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("applicationChoices"));
    auto *search = picker.findChild<QLineEdit *>(QStringLiteral("applicationSearch"));
    auto *confirm = picker.findChild<QPushButton *>(QStringLiteral("confirmApplications"));
    QVERIFY(list && search && confirm);
    auto *alpha = application(list, QStringLiteral("fixture.alpha"));
    auto *beta = application(list, QStringLiteral("fixture.beta"));
    QVERIFY(alpha && beta);
    // AX changes the native selection; it need not synthesize a clicked signal.
    alpha->setSelected(true);
    search->setText(QStringLiteral("Beta"));
    QVERIFY(alpha->isHidden());
    beta->setSelected(true);
    QCOMPARE(picker.selectedApplications(), (QStringList{QStringLiteral("fixture.alpha"), QStringLiteral("fixture.beta")}));
    QCOMPARE(confirm->text(), QStringLiteral("添加 2 个应用"));
    search->setText(QStringLiteral("no matching application"));
    QVERIFY(alpha->isHidden() && beta->isHidden());
    QCOMPARE(picker.selectedApplications().size(), 2);
    QVERIFY(confirm->isEnabled());
    search->clear();
    QVERIFY(alpha->isSelected() && beta->isSelected());
    alpha->setSelected(false);
    QCOMPARE(picker.selectedApplications(), QStringList{QStringLiteral("fixture.beta")});
    QCOMPARE(confirm->text(), QStringLiteral("添加 1 个应用"));
  }

  void keyboardSpaceUsesTheSameNativeSelection()
  {
    ApplicationPicker picker({}, catalog());
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("applicationChoices"));
    auto *confirm = picker.findChild<QPushButton *>(QStringLiteral("confirmApplications"));
    QVERIFY(list && confirm);
    auto *alpha = application(list, QStringLiteral("fixture.alpha"));
    QVERIFY(alpha);
    picker.show();
    QApplication::processEvents();
    list->setCurrentItem(alpha, QItemSelectionModel::NoUpdate);
    list->setFocus();
    QTest::keyClick(list, Qt::Key_Space);
    QVERIFY(alpha->isSelected());
    QCOMPARE(picker.selectedApplications(), QStringList{QStringLiteral("fixture.alpha")});
    QVERIFY(confirm->isEnabled());
    QTest::keyClick(list, Qt::Key_Space);
    QVERIFY(!alpha->isSelected());
    QVERIFY(!confirm->isEnabled());
  }

  void existingApplicationsStayInOrderAndCannotBeRemovedHere()
  {
    const QStringList existing{QStringLiteral("fixture.gamma"), QStringLiteral("fixture.alpha")};
    ApplicationPicker picker(existing, catalog());
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("applicationChoices"));
    auto *confirm = picker.findChild<QPushButton *>(QStringLiteral("confirmApplications"));
    QVERIFY(list && confirm);
    auto *alpha = application(list, QStringLiteral("fixture.alpha"));
    auto *beta = application(list, QStringLiteral("fixture.beta"));
    QVERIFY(alpha && beta);
    QVERIFY(alpha->text().contains(QStringLiteral("Alpha")));
    QVERIFY(alpha->text().contains(QStringLiteral("已添加")));
    QVERIFY(!(alpha->flags() & Qt::ItemIsSelectable));
    QCOMPARE(picker.selectedApplications(), existing);
    QVERIFY(!confirm->isEnabled());
    picker.show();
    QApplication::processEvents();
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(alpha).center());
    QCOMPARE(picker.selectedApplications(), existing);
    QVERIFY(!confirm->isEnabled());
    beta->setSelected(true);
    QCOMPARE(picker.selectedApplications(), (existing + QStringList{QStringLiteral("fixture.beta")}));
    QCOMPARE(confirm->text(), QStringLiteral("添加 1 个应用"));
  }

  void cancellingDoesNotChangeTheCallersSelection()
  {
    const QStringList initial{QStringLiteral("fixture.alpha")};
    QStringList callerSelection = initial;
    ApplicationPicker picker(callerSelection, catalog());
    auto *list = picker.findChild<QListWidget *>(QStringLiteral("applicationChoices"));
    auto *buttons = picker.findChild<QDialogButtonBox *>();
    QVERIFY(list && buttons);
    application(list, QStringLiteral("fixture.beta"))->setSelected(true);
    QSignalSpy accepted(&picker, &QDialog::accepted);
    QSignalSpy rejected(&picker, &QDialog::rejected);
    buttons->button(QDialogButtonBox::Cancel)->click();
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(accepted.count(), 0);
    if (picker.result() == QDialog::Accepted) callerSelection = picker.selectedApplications();
    QCOMPARE(callerSelection, initial);
  }

private:
  static QList<ApplicationInfo> catalog()
  {
    return {{QStringLiteral("fixture.alpha"), QStringLiteral("Alpha"), {}},
            {QStringLiteral("fixture.beta"), QStringLiteral("Beta"), {}},
            {QStringLiteral("fixture.gamma"), QStringLiteral("Gamma"), {}}};
  }

  static QListWidgetItem *application(QListWidget *list, const QString &id)
  {
    for (int i = 0; i < list->count(); ++i)
      if (list->item(i)->data(Qt::UserRole).toString() == id) return list->item(i);
    return nullptr;
  }
};

QTEST_MAIN(PersonalDialogTests)
#include "PersonalDialogTests.moc"
