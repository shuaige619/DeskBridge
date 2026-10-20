/* SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception */
#pragma once

#include "common/Enums.h"
#include "common/ScreenLayout.h"
#include "common/ShortcutRules.h"
#include <QWidget>
#include <QVariantMap>

class QCheckBox;
class QAction;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QToolBar;

namespace deskflow::gui::personal {
class DeviceIllustration;
class LoginStartup;
class ShortcutPage;
class AdvancedPage;

class PersonalWindow : public QWidget
{
  Q_OBJECT
public:
  enum Page { Connection, Shortcuts, General, Advanced };
  explicit PersonalWindow(QWidget *parent = nullptr, LoginStartup *service = nullptr);
  ShortcutPage *shortcutsPage() const { return m_shortcuts; }
  AdvancedPage *advancedPage() const { return m_advanced; }
  void showPage(Page page);
  void setConnectionStatus(deskflow::core::ConnectionState connection, deskflow::core::ProcessState process,
                           bool tlsEnabled, bool secure, bool canConnect, const QString &host, const QString &macName);
  void setMappingStatus(bool enabled, int enabledRules, const QString &fullShortcutStatus = {});
  void setDeviceContext(shortcuts::DeviceOS sourceOS, bool server);
  void setScreenLayout(const layout::Snapshot &snapshot, bool pending = false, const QString &feedback = {});
  void reloadPreferences();
  void finishPreferencesApply(bool success, const QString &message);
  void setPreferencesStatus(const QString &message);
  void setConnectionFeedback(const QString &message);
  void setConfigurationBusy(bool busy, bool allowDisconnect = false);
  bool hasPendingPreferences() const { return m_preferencesDirty; }

Q_SIGNALS:
  void connectRequested();
  void disconnectRequested();
  void connectionOptionsRequested();
  void preferencesApplyRequested(const QVariantMap &changes);
  void screenLayoutRequested(deskflow::layout::Direction direction);
  void screenMoveRequested(const deskflow::layout::Move &move);

protected:
  void changeEvent(QEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  QWidget *makeConnectionPage();
  QWidget *makeGeneralPage();
  void refreshAppearance();
  void refreshScreenLayout();
  void markPreferencesDirty();
  void savePreferences();
  QVariantMap preferenceChanges() const;
  void refreshLoginStartup();
  void updateLoginStartup();
  LoginStartup *m_loginStartup = nullptr;
  QCheckBox *m_loginStartupSwitch = nullptr;
  QLabel *m_loginStartupHint = nullptr;
  QPushButton *m_loginStartupSettings = nullptr;
  QString m_loginStartupFeedback;
  bool m_loginStartupFailed = false;
  QStackedWidget *m_pages = nullptr;
  ShortcutPage *m_shortcuts = nullptr;
  AdvancedPage *m_advanced = nullptr;
  QToolBar *m_toolbar = nullptr;
  QLabel *m_title = nullptr;
  QLabel *m_subtitle = nullptr;
  QLabel *m_status = nullptr;
  QLabel *m_security = nullptr;
  QLabel *m_sidebarStatus = nullptr;
  QLabel *m_connectionHint = nullptr;
  QLabel *m_connectionFeedback = nullptr;
  QLabel *m_layoutNote = nullptr;
  QLabel *m_mappingDetail = nullptr;
  QLabel *m_mappingBadge = nullptr;
  QLabel *m_preferencesMessage = nullptr;
  QPushButton *m_primaryButton = nullptr;
  QAction *m_connectionAction = nullptr;
  QPushButton *m_mappingRow = nullptr;
  QPushButton *m_savePreferences = nullptr;
  QPushButton *m_revertPreferences = nullptr;
  QPushButton *m_connectionOptions = nullptr;
  QList<QPushButton *> m_navigation;
  DeviceIllustration *m_devices = nullptr;
  QCheckBox *m_autoConnect = nullptr;
  QCheckBox *m_autoHide = nullptr;
  QCheckBox *m_closeToTray = nullptr;
  QCheckBox *m_invertScroll = nullptr;
  QCheckBox *m_invertXScroll = nullptr;
  QCheckBox *m_preventSleep = nullptr;
  QDoubleSpinBox *m_scrollSpeed = nullptr;
  QDoubleSpinBox *m_xScrollSpeed = nullptr;
  bool m_running = false;
  bool m_loadingPreferences = false;
  bool m_preferencesDirty = false;
  QVariantMap m_preferencesBaseline;
  bool m_updatingAppearance = false;
  bool m_configurationBusy = false;
  bool m_allowDisconnectWhileBusy = false;
  bool m_connectionCanInteract = false;
  bool m_serverMode = false;
  layout::Snapshot m_screenLayout;
  bool m_layoutPending = false;
  QString m_layoutFeedback;
  shortcuts::DeviceOS m_sourceOS = shortcuts::DeviceOS::Unknown;
};
} // namespace deskflow::gui::personal
