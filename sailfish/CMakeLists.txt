# Sailfish OS build: daemon (wearoutd) + Silica UI (harbour-wearout).
# Built from the top-level CMakeLists with -DWEAROUT_BUILD_SAILFISH=ON,
# normally via rpm/harbour-wearout.spec inside the Sailfish SDK.

find_package(Qt5 REQUIRED COMPONENTS Core DBus Quick Qml)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SAILFISHAPP REQUIRED IMPORTED_TARGET sailfishapp)

set(CMAKE_AUTOMOC ON)

add_executable(wearoutd
    daemon/main.cpp
    daemon/daemon.cpp
    daemon/bluezprofile.cpp
    daemon/watchlink.cpp
)
target_link_libraries(wearoutd PRIVATE wearout_core Qt5::Core Qt5::DBus)

add_executable(harbour-wearout app/main.cpp)
target_link_libraries(harbour-wearout PRIVATE Qt5::Quick Qt5::Qml PkgConfig::SAILFISHAPP)

include(GNUInstallDirs)
install(TARGETS wearoutd harbour-wearout RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY app/qml DESTINATION share/harbour-wearout)
install(FILES harbour-wearout.desktop DESTINATION share/applications)
install(FILES wearoutd.service DESTINATION lib/systemd/user)
