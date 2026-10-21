add_executable(freearc-ex-gui
    main.cpp
)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0)
target_link_libraries(freearc-ex-gui PRIVATE fa::core PkgConfig::GTKMM)

set_target_properties(freearc-ex-gui PROPERTIES OUTPUT_NAME "freearc-ex")
