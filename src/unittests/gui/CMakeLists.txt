# SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
# SPDX-License-Identifier: MIT

add_subdirectory(config)
add_subdirectory(core)

if(DESKBRIDGE_PERSONAL_BUILD)
  create_test(
    NAME TrayIconTests
    DEPENDS gui
    SOURCE TrayIconTests.cpp
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
  )
  target_sources(TrayIconTests PRIVATE ../../apps/res/deskbridge.qrc)
endif()

create_test(
  NAME AdvancedPageTests
  DEPENDS gui
  SOURCE AdvancedPageTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME PreferenceChangesTests
  DEPENDS gui
  SOURCE PreferenceChangesTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME PreferencesApplyControllerTests
  DEPENDS gui
  SOURCE PreferencesApplyControllerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ScreenLayoutPersistenceTests
  DEPENDS gui
  SOURCE ScreenLayoutPersistenceTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME LoginStartupTests
  DEPENDS gui
  SOURCE LoginStartupTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME PersonalDialogTests
  DEPENDS gui
  SOURCE PersonalDialogTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME PersonalWindowTests
  DEPENDS gui
  SOURCE PersonalWindowTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ShortcutPageTests
  DEPENDS gui
  SOURCE ShortcutPageTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ChordRecorderTests
  DEPENDS gui
  SOURCE ChordRecorderTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME ShortcutApplyControllerTests
  DEPENDS gui
  SOURCE ShortcutApplyControllerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME LoggerTests
  DEPENDS gui
  SOURCE LoggerTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)

create_test(
  NAME DeviceIllustrationTests
  DEPENDS gui
  SOURCE DeviceIllustrationTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/gui"
)
