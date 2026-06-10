idf_component_register(
    SRCS "main.c" "ui_reader.cpp" "book_loader.cpp" "app_settings.cpp" "ui_menu.cpp" "autorotate.cpp"
    INCLUDE_DIRS ".")

set_source_files_properties(
    ${LV_DEMOS_SOURCES}
    PROPERTIES COMPILE_OPTIONS
    -DLV_LVGL_H_INCLUDE_SIMPLE)
