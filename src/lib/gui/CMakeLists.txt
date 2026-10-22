# SPDX-FileCopyrightText: (C) 2024 - 2026 Deskflow Developers
# SPDX-FileCopyrightText: (C) 2024 Symless Ltd
# SPDX-License-Identifier: MIT

set(target gui)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(APPLE)
  set (platform_sources
    OSXHelpers.h
    OSXHelpers.mm
    personal/LoginStartupMac.mm
  )
endif()

add_library(${target} STATIC
  ${platform_sources}
  ComputerSetupModel.cpp
  ComputerSetupModel.h
  Diagnostic.cpp
  Diagnostic.h
  FileTail.cpp
  FileTail.h
  Logger.cpp
  Logger.h
  MainWindow.cpp
  MainWindow.h
  MainWindowState.h
  Messages.cpp
  Messages.h
  StyleUtils.h
  personal/PersonalWindow.cpp
  personal/PersonalWindow.h
  personal/DialogStyle.h
  personal/ConnectionOptionsDialog.cpp
  personal/ConnectionOptionsDialog.h
  personal/ReceiverDeviceControlDialog.cpp
  personal/ReceiverDeviceControlDialog.h
  personal/ConnectionIdentity.cpp
  personal/ConnectionIdentity.h
  personal/AdvancedPage.cpp
  personal/AdvancedPage.h
  personal/PreferenceChanges.cpp
  personal/PreferenceChanges.h
  personal/PreferencesApplyController.cpp
  personal/PreferencesApplyController.h
  personal/PersonalStyle.cpp
  personal/PersonalStyle.h
  personal/DeviceIllustration.cpp
  personal/DeviceIllustration.h
  personal/ShortcutPage.cpp
  personal/ShortcutPage.h
  personal/RuleEditor.cpp
  personal/RuleEditor.h
  personal/ChordRecorder.cpp
  personal/ChordRecorder.h
  personal/ApplicationPicker.cpp
  personal/ApplicationPicker.h
  personal/ShortcutApplyController.cpp
  personal/ShortcutApplyController.h
  personal/ScreenLayoutPersistence.cpp
  personal/ScreenLayoutPersistence.h
  personal/ToggleSwitch.cpp
  personal/ToggleSwitch.h
  personal/LoginStartup.cpp
  personal/LoginStartup.h
  personal/LoginStartup_p.h
  TlsUtility.cpp
  TlsUtility.h
  VersionChecker.cpp
  VersionChecker.h
  config/ComputerConfig.h
  config/Computer.cpp
  config/Computer.h
  config/ComputerList.cpp
  config/ComputerList.h
  config/ServerConfig.cpp
  config/ServerConfig.h
  core/CoreProcess.cpp
  core/CoreProcess.h
  core/NetworkMonitor.cpp
  core/NetworkMonitor.h
  dialogs/AboutDialog.cpp
  dialogs/AboutDialog.h
  dialogs/AboutDialog.ui
  dialogs/ActionDialog.cpp
  dialogs/ActionDialog.h
  dialogs/ComputerSettingsDialog.cpp
  dialogs/ComputerSettingsDialog.h
  dialogs/FingerprintDialog.cpp
  dialogs/FingerprintDialog.h
  dialogs/HelpDialog.cpp
  dialogs/HelpDialog.h
  dialogs/HotkeyDialog.cpp
  dialogs/HotkeyDialog.h
  dialogs/ServerConfigDialog.cpp
  dialogs/ServerConfigDialog.h
  dialogs/ServerConfigView.h
  dialogs/EditorViews.h
  dialogs/SupportViews.h
  ipc/IpcClient.cpp
  ipc/IpcClient.h
  ipc/DaemonIpcClient.cpp
  ipc/DaemonIpcClient.h
  ipc/CoreIpcClient.cpp
  ipc/CoreIpcClient.h
  validators/AliasValidator.cpp
  validators/AliasValidator.h
  validators/ComputerDuplicationsValidator.cpp
  validators/ComputerDuplicationsValidator.h
  validators/ComputerLineNameValidator.cpp
  validators/ComputerLineNameValidator.h
  validators/ComputerNameValidator.cpp
  validators/ComputerNameValidator.h
  validators/EmptyStringValidator.cpp
  validators/EmptyStringValidator.h
  validators/IStringValidator.cpp
  validators/IStringValidator.h
  validators/LineEditValidator.cpp
  validators/LineEditValidator.h
  validators/SpacesValidator.cpp
  validators/SpacesValidator.h
  validators/ValidationError.cpp
  validators/ValidationError.h
  validators/IpAddressValidator.cpp
  validators/IpAddressValidator.h
  widgets/ComputerSetupView.cpp
  widgets/ComputerSetupView.h
  widgets/KeySequenceWidget.cpp
  widgets/KeySequenceWidget.h
  widgets/LogDock.cpp
  widgets/LogDock.h
  widgets/LogWidget.h
  widgets/LogWidget.cpp
  widgets/NewComputerWidget.cpp
  widgets/NewComputerWidget.h
  widgets/RemoveComputerWidget.cpp
  widgets/RemoveComputerWidget.h
  widgets/SearchWidget.h
  widgets/SearchWidget.cpp
  widgets/StatusBar.cpp
  widgets/StatusBar.h
  widgets/SettingsDialogButtonBox.cpp
  widgets/SettingsDialogButtonBox.h
)

target_link_libraries(${target}
  PUBLIC
    common
    platform
    net
    Qt6::Widgets
)

if(APPLE)
  find_library(SERVICE_MANAGEMENT_LIBRARY ServiceManagement REQUIRED)
  find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
  target_link_libraries(${target} PRIVATE ${SERVICE_MANAGEMENT_LIBRARY} ${FOUNDATION_LIBRARY})
endif()
