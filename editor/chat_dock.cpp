/**************************************************************************/
/*  chat_dock.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "chat_dock.h"

#include "core/io/config_file.h"
#include "core/input/input_event.h" // For key codes
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "editor/editor_string_names.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"

void ChatDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input_text_submitted"), &ChatDock::_input_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_send_button_pressed"), &ChatDock::_on_send_button_pressed);
}

void ChatDock::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_READY: {
			EditorNode::get_singleton()->connect("save_editor_layout_to_config", callable_mp(this, &ChatDock::_save_layout_to_config));
			EditorNode::get_singleton()->connect("load_editor_layout_from_config", callable_mp(this, &ChatDock::_load_layout_from_config));

			// Add welcome message
			add_message("AI Assistant", "Hello! I'm your coding assistant. How can I help you today?", true);
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible_in_tree() && input_field) {
				input_field->call_deferred(SNAME("grab_focus"));
			}
		} break;

		case NOTIFICATION_THEME_CHANGED: {
			if (chat_display) {
				chat_display->add_theme_font_override("normal_font", get_theme_font(SNAME("main"), SNAME("EditorFonts")));
				chat_display->add_theme_font_size_override("normal_font_size", get_theme_font_size(SNAME("main_size"), SNAME("EditorFonts")));
			}
			if (send_button) {
				send_button->set_button_icon(get_theme_icon(SNAME("ArrowRight"), SNAME("EditorIcons")));
			}
		} break;
	}
}

void ChatDock::_input_text_submitted(const String &p_text) {
	if (p_text.strip_edges().is_empty()) {
		return;
	}
	
	_send_message();
}

void ChatDock::_input_special_key_pressed(const Ref<InputEvent> &p_event) {
	Ref<InputEventKey> k = p_event;
	if (k.is_valid() && k->is_pressed() && !k->is_echo()) {
		if (k->get_keycode() == Key::UP) {
			// Navigate up message history
			if (history_position < message_history.size() - 1) {
				history_position++;
				input_field->set_text(message_history[history_position]);
				input_field->set_caret_column(input_field->get_text().length());
			}
		} else if (k->get_keycode() == Key::DOWN) {
			// Navigate down message history
			if (history_position > 0) {
				history_position--;
				input_field->set_text(message_history[history_position]);
				input_field->set_caret_column(input_field->get_text().length());
			} else if (history_position == 0) {
				history_position = -1;
				input_field->set_text("");
			}
		}
	}
}

void ChatDock::_on_send_button_pressed() {
	_send_message();
}

void ChatDock::_send_message() {
	String message = input_field->get_text().strip_edges();
	if (message.is_empty()) {
		return;
	}

	// Add to history
	message_history.insert(0, message);
	if (message_history.size() > 30) { // Limit history size
		message_history.resize(30);
	}
	history_position = -1;

	// Display user message
	add_message("You", message);
	
	// Clear input field
	input_field->clear();
	
	// Generate AI response (in a real implementation, this would call an API)
	String ai_response = "I'm a simple mock assistant built into the Godot editor. In a full implementation, this would make an API call to a real AI service like Claude or GPT.";
	
	// Add AI response with a small delay to simulate thinking
	call_deferred("add_message", "AI Assistant", ai_response, true);
}

void ChatDock::add_message(const String &p_from, const String &p_message, bool p_is_ai) {
	if (!chat_display) {
		return;
	}

	Color user_color = Color(0.5, 0.8, 1.0);
	Color ai_color = Color(0.5, 1.0, 0.5);
	
	// Add sender name with appropriate color
	Color name_color = p_is_ai ? ai_color : user_color;
	chat_display->push_color(name_color);
	chat_display->push_bold();
	chat_display->add_text(p_from + ":");
	chat_display->pop();
	chat_display->pop();
	chat_display->add_newline();
	
	// Add message text
	chat_display->add_text(p_message);
	chat_display->add_newline();
	chat_display->add_newline();
	
	// Scroll to bottom
	chat_display->scroll_to_line(chat_display->get_paragraph_count() - 1);
}

void ChatDock::_save_layout_to_config(Ref<ConfigFile> p_layout, const String &p_section) const {
	// Any state to save would go here
}

void ChatDock::_load_layout_from_config(Ref<ConfigFile> p_layout, const String &p_section) {
	// Any state to load would go here
}

ChatDock::ChatDock() {
	set_name("Chat");
	
	// Chat display area (messages)
	chat_display = memnew(RichTextLabel);
	chat_display->set_v_size_flags(SIZE_EXPAND_FILL);
	chat_display->set_selection_enabled(true);
	chat_display->set_context_menu_enabled(true);
	chat_display->set_focus_mode(FOCUS_NONE);
	chat_display->set_custom_minimum_size(Size2(200, 100));
	chat_display->set_scroll_follow(true);
	add_child(chat_display);
	
	// Input container (text field + send button)
	input_container = memnew(HBoxContainer);
	add_child(input_container);
	
	// Input text field
	input_field = memnew(LineEdit);
	input_field->set_h_size_flags(SIZE_EXPAND_FILL);
	input_field->set_placeholder("Type a message...");
	input_field->connect("text_submitted", callable_mp(this, &ChatDock::_input_text_submitted));
	input_field->connect("gui_input", callable_mp(this, &ChatDock::_input_special_key_pressed));
	input_container->add_child(input_field);
	
	// Send button
	send_button = memnew(Button);
	send_button->set_flat(true);
	send_button->connect("pressed", callable_mp(this, &ChatDock::_on_send_button_pressed));
	input_container->add_child(send_button);
} 