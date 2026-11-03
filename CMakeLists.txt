cmake_minimum_required(VERSION 3.25)
project(Rolidan VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets LinguistTools Test)
qt_standard_project_setup(REQUIRES 6.5)

# Core library: everything except main(), so tests can link it.
qt_add_library(rolidan_core STATIC
    src/annotation.h src/annotation.cpp
    src/toolid.h
    src/tooldefaults.h src/tooldefaults.cpp
    src/propertypanel.h src/propertypanel.cpp
    src/document.h src/document.cpp
    src/retouch.h src/retouch.cpp
    src/filters.h src/filters.cpp
    src/filterdialog.h src/filterdialog.cpp
    src/batch.h src/batch.cpp
    src/zip.h src/zip.cpp
    src/project.h src/project.cpp
    src/recovery.h src/recovery.cpp
    src/sizedialogs.h src/sizedialogs.cpp
    src/history.h src/history.cpp
    src/imageio.h src/imageio.cpp
    src/codecs.h src/codecs.cpp
    src/icons.h src/icons.cpp
    src/notespanel.h src/notespanel.cpp
    src/clipboard.h src/clipboard.cpp
    src/i18n.h src/i18n.cpp
    src/settings.h src/settings.cpp
    src/canvas.h src/canvas.cpp
    src/tools.h src/tools.cpp
    src/toast.h src/toast.cpp
    src/exportdialog.h src/exportdialog.cpp
    src/settingsdialog.h src/settingsdialog.cpp
    src/mainwindow.h src/mainwindow.cpp
    src/application.h src/application.cpp
)
target_include_directories(rolidan_core PUBLIC src)
target_link_libraries(rolidan_core PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(rolidan_core PUBLIC ROLIDAN_VERSION="${PROJECT_VERSION}")
target_compile_options(rolidan_core PRIVATE -Wall -Wextra)

# Direct encoders for the JPEG / WebP options Qt's plugins do not expose
# (spec F5). Optional: without them the Qt plugins are used.
find_package(JPEG)
if(JPEG_FOUND)
    target_link_libraries(rolidan_core PRIVATE JPEG::JPEG)
    target_compile_definitions(rolidan_core PRIVATE ROLIDAN_HAVE_JPEG)
else()
    message(STATUS "libjpeg headers not found (apt install libjpeg62-turbo-dev): JPEG options limited")
endif()
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(WEBP IMPORTED_TARGET libwebp)
endif()
if(WEBP_FOUND)
    target_link_libraries(rolidan_core PRIVATE PkgConfig::WEBP)
    target_compile_definitions(rolidan_core PRIVATE ROLIDAN_HAVE_WEBP)
else()
    message(STATUS "libwebp headers not found (apt install libwebp-dev): WebP options limited")
endif()

qt_add_executable(rolidan src/main.cpp)
target_link_libraries(rolidan PRIVATE rolidan_core)

# Built-in translations (spec L): cs, de, es, ja are compiled into the binary.
set(ROLIDAN_LANGS cs de es ja)
set(ROLIDAN_TS_FILES)
foreach(lang IN LISTS ROLIDAN_LANGS)
    list(APPEND ROLIDAN_TS_FILES i18n/rolidan_${lang}.ts)
endforeach()
qt_add_translations(rolidan_core
    SOURCE_TARGETS rolidan rolidan_core
    TS_FILES ${ROLIDAN_TS_FILES}
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete -locations none)

# Qt's own translations of standard dialogs, embedded too.
set(_qt_tr_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
set(_qtbase_qm)
foreach(lang IN LISTS ROLIDAN_LANGS)
    if(EXISTS "${_qt_tr_dir}/qtbase_${lang}.qm")
        list(APPEND _qtbase_qm "${_qt_tr_dir}/qtbase_${lang}.qm")
    else()
        message(WARNING "qtbase_${lang}.qm not found in ${_qt_tr_dir} (install qt6-translations-l10n)")
    endif()
endforeach()
if(_qtbase_qm)
    qt_add_resources(rolidan_core qtbase_translations PREFIX /i18n BASE "${_qt_tr_dir}" FILES ${_qtbase_qm})
endif()

include(GNUInstallDirs)
install(TARGETS rolidan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES data/rolidan.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/icons/rolidan.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
install(FILES data/rolidan-mime.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages RENAME rolidan.xml)
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/rolidan RENAME copyright)

# Debian package: cmake --build build --target package
set(CPACK_GENERATOR DEB)
set(CPACK_PACKAGE_NAME rolidan)
set(CPACK_PACKAGE_CONTACT "Rolidan developers")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fast image editor: paste, annotate, save")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION graphics)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Plugins are loaded at runtime, so shlibdeps cannot see them.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-image-formats-plugins, qt6-wayland, qt6-qpa-plugins")
set(CPACK_PACKAGING_INSTALL_PREFIX /usr)
include(CPack)

enable_testing()
qt_add_executable(tst_core tests/tst_core.cpp)
target_link_libraries(tst_core PRIVATE rolidan_core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)
set_tests_properties(tst_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(tst_gui tests/tst_gui.cpp)
target_link_libraries(tst_gui PRIVATE rolidan_core Qt6::Test)
add_test(NAME tst_gui COMMAND tst_gui)
set_tests_properties(tst_gui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
