/**************************************************************************/
/*  chat_dock.h                                                           */
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

#pragma once

#include "core/io/http_client.h"
#include "scene/gui/box_container.h"

class RichTextLabel;
class LineEdit;
class Button;
class ConfigFile;
class HBoxContainer;
class CheckBox;

class ChatDock : public VBoxContainer {
	GDCLASS(ChatDock, VBoxContainer);

private:
	enum RequestState {
		REQUEST_NONE,
		REQUEST_CONNECTING,
		REQUEST_CONNECTED,
		REQUEST_REQUESTING,
		REQUEST_PROCESSING_RESPONSE
	};

	struct HTTPRequestData {
		String host;
		int port;
		String path;
		String body;
		Vector<String> headers;
	};

	RichTextLabel *chat_display = nullptr;
	HBoxContainer *input_container = nullptr;
	LineEdit *input_field = nullptr;
	Button *send_button = nullptr;
	CheckBox *include_all_files_checkbox = nullptr;
	Button *index_project_button = nullptr;

	Ref<HTTPClient> http_client;
	bool waiting_for_response = false;
	RequestState http_request_state = REQUEST_NONE;
	HTTPRequestData http_request_data;
	int process_iterations = 0;
	double request_start_time = 0;

	// For tracking requested files
	Array requested_files;
	bool waiting_for_file_request = false;

	Vector<String> message_history;
	int history_position = -1;
	int thinking_message_id = -1;

	void _process_http_request();
	String _get_file_content(const String &p_path);
	void _send_file_content(const Array &p_files);
	void _make_second_api_call(const String &p_prompt, const Dictionary &p_file_contents);
	void _make_direct_api_call(const String &p_prompt);

	void _send_message();
	void _handle_ai_response(const String &p_response);
	void _print_token_usage(const Dictionary &p_token_usage);
	void _input_text_submitted(const String &p_text);
	void _input_special_key_pressed(const Ref<InputEvent> &p_event);
	void _on_send_button_pressed();
	void _on_index_project_button_pressed();

	void _save_layout_to_config(Ref<ConfigFile> p_layout, const String &p_section) const;
	void _load_layout_from_config(Ref<ConfigFile> p_layout, const String &p_section);

protected:
	void _notification(int p_notification);
	static void _bind_methods();

public:
	void add_message(const String &p_from, const String &p_message, bool p_is_ai = false);
	void add_formatted_ai_response(const String &p_message);
	ChatDock();
};