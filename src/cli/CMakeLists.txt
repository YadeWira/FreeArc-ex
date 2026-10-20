add_executable(arc-ex main.cpp)
target_link_libraries(arc-ex PRIVATE fa::core)
set_target_properties(arc-ex PROPERTIES OUTPUT_NAME "arc-ex")
