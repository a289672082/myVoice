# Embedded FTXUI v7.0.3 (MIT): sourced from ArthurSonzogni/FTXUI.
# This build intentionally never downloads code or requires an installed package.
file(GLOB_RECURSE FTXUI_SCREEN_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ftxui/screen/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ftxui/util/*.cpp")
file(GLOB_RECURSE FTXUI_DOM_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ftxui/dom/*.cpp")
file(GLOB_RECURSE FTXUI_COMPONENT_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/ftxui/component/*.cpp")
foreach(_target SCREEN DOM COMPONENT)
  string(TOLOWER "${_target}" _lower)
  add_library(ftxui_${_lower} STATIC ${FTXUI_${_target}_SRC})
  target_compile_features(ftxui_${_lower} PUBLIC cxx_std_17)
  target_include_directories(ftxui_${_lower} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_compile_definitions(ftxui_${_lower} PRIVATE "IS_FTXUI_${_target}_IMPL=1")
  if(MSVC)
    target_compile_options(ftxui_${_lower} PUBLIC /utf-8 PRIVATE /wd4244 /wd4267)
    target_compile_definitions(ftxui_${_lower} PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
  endif()
  set_property(TARGET ftxui_${_lower} PROPERTY POSITION_INDEPENDENT_CODE ON)
  add_library(ftxui::${_lower} ALIAS ftxui_${_lower})
endforeach()
target_link_libraries(ftxui_dom PUBLIC ftxui_screen)
target_link_libraries(ftxui_component PUBLIC ftxui_dom)
