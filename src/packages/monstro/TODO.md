# Monstro Package TODO

This file tracks Monstro functions that still need a Mobius package API.
It focuses on missing package-facing native APIs, not the script-side helper
constructors and enum tables already present in `monstro.mob`.

Already broadly covered:

- context create / destroy / closed state
- display size / frame lifecycle / draw data extraction
- input injection and basic input state queries
- window lifecycle and basic window queries
- layout / cursor / columns basics
- item state queries / disabled scopes
- a first-pass set of common widgets:
  button, label, checkbox, radio button, sliders, knob, image, input int/float/double,
  progress bar, tooltip, separators

## High Priority Missing APIs

These are the biggest remaining gaps for making the package broadly useful.

### Core / Context Plumbing

- [ ] verify whether `monstro_init()` and `monstro_shutdown()` should be exposed
      or intentionally skipped
- [ ] `monstro_context_get_draw_buffer`
- [ ] `monstro_draw_buffer_set_vertex_buffer`
- [ ] `monstro_draw_buffer_set_index_buffer`

### Missing Object Wrappers

Before much of the remaining surface can be exposed cleanly, add userdata
wrappers for:

- [ ] `MonstroCanvas`
- [ ] `MonstroFontManager`
- [ ] `MonstroFont`
- [ ] `MonstroFileDialogState`
- [ ] style snapshot/value wrapper strategy for `MonstroStyle*` and the
      per-widget style structs

### Window / Layout Gaps

- [ ] `monstro_get_window_canvas`
- [ ] `monstro_set_last_item`
- [ ] `monstro_begin_columns_ex`

### Missing Common Widgets

- [ ] `monstro_button_ex`
- [ ] `monstro_image_button_ex`
- [ ] `monstro_image_button`
- [ ] `monstro_invisible_button_ex`
- [ ] `monstro_invisible_button`
- [ ] `monstro_label_colored`
- [ ] `monstro_input_text`
- [ ] `monstro_input_text_multiline`
- [ ] `monstro_selectable`
- [ ] `monstro_selectable_sized`
- [ ] `monstro_begin_combo`
- [ ] `monstro_end_combo`
- [ ] `monstro_combo`

### Menus / Popups / Trees / Grouping

- [ ] `monstro_begin_menu_bar`
- [ ] `monstro_end_menu_bar`
- [ ] `monstro_begin_menu`
- [ ] `monstro_end_menu`
- [ ] `monstro_menu_item`
- [ ] `monstro_open_popup`
- [ ] `monstro_open_popup_ex`
- [ ] `monstro_close_current_popup`
- [ ] `monstro_begin_popup`
- [ ] `monstro_begin_popup_modal`
- [ ] `monstro_begin_popup_context_item`
- [ ] `monstro_begin_popup_context_window`
- [ ] `monstro_begin_popup_context_void`
- [ ] `monstro_end_popup`
- [ ] `monstro_is_popup_open`
- [ ] `monstro_begin_tree_node`
- [ ] `monstro_begin_tree_node_id`
- [ ] `monstro_end_tree_node`
- [ ] `monstro_tree_leaf`
- [ ] `monstro_collapsing_header`
- [ ] `monstro_get_tree_indent`
- [ ] `monstro_set_tree_indent`
- [ ] `monstro_begin_group`
- [ ] `monstro_end_group`

### Tabs / Tables / Color UI

- [ ] `monstro_begin_tab_bar`
- [ ] `monstro_end_tab_bar`
- [ ] `monstro_begin_tab_item`
- [ ] `monstro_end_tab_item`
- [ ] `monstro_begin_table`
- [ ] `monstro_end_table`
- [ ] `monstro_table_setup_column`
- [ ] `monstro_table_headers_row`
- [ ] `monstro_table_next_row`
- [ ] `monstro_table_next_column`
- [ ] `monstro_table_set_column_index`
- [ ] `monstro_color_picker`
- [ ] `monstro_color_button`

### File Dialog

- [ ] `monstro_file_dialog_create`
- [ ] `monstro_file_dialog_destroy`
- [ ] `monstro_file_dialog_open`
- [ ] `monstro_file_dialog_draw`
- [ ] `monstro_file_dialog_get_path`
- [ ] `monstro_file_dialog_is_open`

## Medium Priority Missing APIs

These are important for fuller parity, but are gated more by wrapper/object
design than by raw implementation effort alone.

### Canvas / Draw API

- [ ] `monstro_push_clip_rect`
- [ ] `monstro_pop_clip_rect`
- [ ] `monstro_draw_line`
- [ ] `monstro_draw_rect`
- [ ] `monstro_draw_rect_filled`
- [ ] `monstro_draw_rect_filled_multi_color`
- [ ] `monstro_draw_triangle`
- [ ] `monstro_draw_triangle_filled`
- [ ] `monstro_draw_circle`
- [ ] `monstro_draw_circle_filled`
- [ ] `monstro_draw_quad`
- [ ] `monstro_draw_quad_filled`
- [ ] `monstro_draw_polyline`
- [ ] `monstro_draw_convex_poly_filled`
- [ ] `monstro_draw_bezier_cubic`
- [ ] `monstro_draw_bezier_quadratic`
- [ ] `monstro_draw_text`
- [ ] `monstro_draw_text_aligned`
- [ ] `monstro_draw_text_wrapped`
- [ ] `monstro_draw_image`
- [ ] `monstro_draw_image_rounded`
- [ ] `monstro_draw_nine_patch`
- [ ] `monstro_draw_custom`
- [ ] `monstro_path_clear`
- [ ] `monstro_path_move_to`
- [ ] `monstro_path_line_to`
- [ ] `monstro_path_arc_to`
- [ ] `monstro_path_bezier_cubic_to`
- [ ] `monstro_path_bezier_quadratic_to`
- [ ] `monstro_path_fill`
- [ ] `monstro_path_stroke`

### Fonts

- [ ] `monstro_font_manager_create`
- [ ] `monstro_font_manager_destroy`
- [ ] `monstro_context_get_font_manager`
- [ ] `monstro_font_load_ttf`
- [ ] `monstro_font_load_ttf_from_file`
- [ ] `monstro_font_load_bitmap`
- [ ] `monstro_font_load_bitmap_from_file`
- [ ] `monstro_font_destroy`
- [ ] `monstro_font_set_atlas_texture`
- [ ] `monstro_font_get_atlas_texture`
- [ ] `monstro_font_add`
- [ ] `monstro_font_find`
- [ ] `monstro_font_measure_text`
- [ ] `monstro_font_measure_text_wrapped`
- [ ] `monstro_font_get_glyph`
- [ ] `monstro_font_get_glyph_count`
- [ ] `monstro_font_get_size`
- [ ] `monstro_font_get_line_height`
- [ ] `monstro_font_get_ascent`
- [ ] `monstro_font_get_descent`
- [ ] `monstro_font_get_atlas_size`

### Icons

- [ ] `monstro_register_icon_atlas`
- [ ] `monstro_register_icon`
- [ ] `monstro_register_icon_font`
- [ ] `monstro_get_icon`
- [ ] `monstro_icon`
- [ ] `monstro_icon_str`

### Style / Themes

- [ ] `monstro_get_style`
- [ ] `monstro_set_style`
- [ ] `monstro_style_set_default`
- [ ] `monstro_style_set_dark`
- [ ] `monstro_style_set_light`
- [ ] `monstro_get_text_style`
- [ ] `monstro_get_button_style`
- [ ] `monstro_get_contextual_button_style`
- [ ] `monstro_get_menu_button_style`
- [ ] `monstro_get_option_style`
- [ ] `monstro_get_checkbox_style`
- [ ] `monstro_get_selectable_style`
- [ ] `monstro_get_slider_style`
- [ ] `monstro_get_knob_style`
- [ ] `monstro_get_progress_style`
- [ ] `monstro_get_property_style`
- [ ] `monstro_get_edit_style`
- [ ] `monstro_get_tree_style`
- [ ] `monstro_get_tab_style`
- [ ] `monstro_get_table_style`
- [ ] `monstro_get_combo_style`
- [ ] `monstro_get_menu_style`
- [ ] `monstro_get_separator_style`
- [ ] `monstro_get_tooltip_style`
- [ ] `monstro_get_chart_style`
- [ ] `monstro_get_scrollh_style`
- [ ] `monstro_get_scrollv_style`
- [ ] `monstro_get_window_style`
- [ ] `monstro_set_text_style`
- [ ] `monstro_set_button_style`
- [ ] `monstro_set_contextual_button_style`
- [ ] `monstro_set_menu_button_style`
- [ ] `monstro_set_option_style`
- [ ] `monstro_set_checkbox_style`
- [ ] `monstro_set_selectable_style`
- [ ] `monstro_set_slider_style`
- [ ] `monstro_set_knob_style`
- [ ] `monstro_set_progress_style`
- [ ] `monstro_set_property_style`
- [ ] `monstro_set_edit_style`
- [ ] `monstro_set_tree_style`
- [ ] `monstro_set_tab_style`
- [ ] `monstro_set_table_style`
- [ ] `monstro_set_combo_style`
- [ ] `monstro_set_menu_style`
- [ ] `monstro_set_separator_style`
- [ ] `monstro_set_tooltip_style`
- [ ] `monstro_set_chart_style`
- [ ] `monstro_set_scrollh_style`
- [ ] `monstro_set_scrollv_style`
- [ ] `monstro_set_window_style`
- [ ] `monstro_push_text_style`
- [ ] `monstro_push_button_style`
- [ ] `monstro_push_contextual_button_style`
- [ ] `monstro_push_menu_button_style`
- [ ] `monstro_push_option_style`
- [ ] `monstro_push_checkbox_style`
- [ ] `monstro_push_selectable_style`
- [ ] `monstro_push_slider_style`
- [ ] `monstro_push_knob_style`
- [ ] `monstro_push_progress_style`
- [ ] `monstro_push_property_style`
- [ ] `monstro_push_edit_style`
- [ ] `monstro_push_tree_style`
- [ ] `monstro_push_tab_style`
- [ ] `monstro_push_table_style`
- [ ] `monstro_push_combo_style`
- [ ] `monstro_push_menu_style`
- [ ] `monstro_push_separator_style`
- [ ] `monstro_push_tooltip_style`
- [ ] `monstro_push_chart_style`
- [ ] `monstro_push_scrollh_style`
- [ ] `monstro_push_scrollv_style`
- [ ] `monstro_push_window_style`
- [ ] `monstro_pop_style`

## Notes

- The biggest remaining structural work is wrapper shape, not just function
  count. Canvas, font, style, and file-dialog APIs need real userdata-backed
  objects or a deliberately chosen table-based representation.
- `monstro_draw_custom` will require callback plumbing similar in spirit to the
  existing callback work elsewhere, but shaped for synchronous canvas draw
  invocation.
- If the goal remains “complete API surface”, this file should shrink only when
  the native package surface exists, not just when `monstro.mob` gets helper
  constructors or enums.
