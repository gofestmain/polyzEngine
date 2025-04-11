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
#include "scene/gui/check_box.h"
#include <iostream> // Add this line for std::cout and std::endl
#include "core/io/json.h" // For JSON parsing
#include "contextlogic/context_utility.h" // Include our context utility

void ChatDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input_text_submitted"), &ChatDock::_input_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_send_button_pressed"), &ChatDock::_on_send_button_pressed);
	ClassDB::bind_method(D_METHOD("add_message"), &ChatDock::add_message);
	ClassDB::bind_method(D_METHOD("_process_http_request"), &ChatDock::_process_http_request);
	ClassDB::bind_method(D_METHOD("_handle_ai_response"), &ChatDock::_handle_ai_response);
	ClassDB::bind_method(D_METHOD("add_formatted_ai_response"), &ChatDock::add_formatted_ai_response);
}

void ChatDock::_notification(int p_notification) {
	switch (p_notification) {
		case NOTIFICATION_READY: {
			// These signals don't exist in EditorNode, so we'll comment them out
			// EditorNode::get_singleton()->connect("save_editor_layout_to_config", callable_mp(this, &ChatDock::_save_layout_to_config));
			// EditorNode::get_singleton()->connect("load_editor_layout_from_config", callable_mp(this, &ChatDock::_load_layout_from_config));

			// Add welcome message
			add_message("AI Assistant", "Hello! I'm your coding assistant. How can I help you today?", true);
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible_in_tree() && input_field) {
				input_field->call_deferred(SNAME("grab_focus"));
			}
		} break;

		case NOTIFICATION_PROCESS: {
			if (waiting_for_response) {
				_process_http_request();
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
	std::cout << "Sending message: " << message.utf8().get_data() << std::endl;
	
	// Display user message
	add_message("You", message);
	
	// Clear input field
	input_field->clear();
	
	// Make HTTP POST request to localhost
	String host = "localhost";
	int port = 3000; // Common development port
	String path = "/api/prompts/godot";
	
	// Enrich the message with context
	bool include_all_files = include_all_files_checkbox->is_pressed();
	String enriched_message = ContextUtility::enrich_prompt(message, include_all_files);
	
	// Create request JSON data
	Dictionary request_data;
	request_data["prompt"] = enriched_message;
	String json_str = JSON::stringify(request_data);

	// Initialize HTTP request data
	http_request_data.host = host;
	http_request_data.port = port;
	http_request_data.path = path;
	http_request_data.body = json_str;
	http_request_data.headers.clear();
	http_request_data.headers.push_back("Content-Type: application/json");
	http_request_data.headers.push_back("Content-Length: " + String::num_int64(json_str.utf8().length()));
	// Remove API key header if not needed
	// http_request_data.headers.push_back("X-Api-Key: YOUR_API_KEY_HERE");
	
	// Setup HTTP client and connect
	if (http_client.is_null()) {
		http_client = HTTPClient::create();
	} else {
		http_client->close();
	}
	
	// Setup headers and make request
	std::cout << "Connecting to: " << host.utf8().get_data() << ":" << port << std::endl;
	Error err = http_client->connect_to_host(host, port);
	if (err != OK) {
		add_message("System", "Failed to connect to API server at " + host + ":" + String::num_int64(port), false);
		return;
	}
	
	// Show a "thinking" message while waiting for response
	add_message("AI Assistant", "Thinking...", true);
	
	// Set to processing mode to handle the connection and request in _process
	http_request_state = REQUEST_CONNECTING;
	waiting_for_response = true;
	set_process(true);
}

void ChatDock::_process_http_request() {
	if (!waiting_for_response || http_client.is_null()) {
		return;
	}

	// Process HTTP client
	http_client->poll();

	// Handle different states
	switch (http_request_state) {
		case REQUEST_CONNECTING: {
			// Check the connection status
			HTTPClient::Status status = http_client->get_status();
			std::cout << "Connection status: " << status << std::endl;
			
			if (status == HTTPClient::STATUS_CONNECTING || status == HTTPClient::STATUS_RESOLVING) {
				// Still connecting, keep waiting
				return;
			} else if (status == HTTPClient::STATUS_CONNECTED) {
				// Connected, move to next state
				http_request_state = REQUEST_CONNECTED;
				std::cout << "Connected to server, preparing to send request" << std::endl;
			} else {
				// Connection failed
				std::cout << "Connection failed with status: " << status << std::endl;
				waiting_for_response = false;
				set_process(false);
				add_message("System", "Failed to connect to server. Status: " + String::num_int64(status), false);
				http_request_state = REQUEST_NONE;
			}
		} break;
		
		case REQUEST_CONNECTED: {
			// Send the request now that we're connected
			std::cout << "Sending request to: " << http_request_data.host.utf8().get_data() << ":" 
				<< http_request_data.port << http_request_data.path.utf8().get_data() << std::endl;
			std::cout << "Request data: " << http_request_data.body.utf8().get_data() << std::endl;
			
			// Prepare the request body
			CharString json_char = http_request_data.body.utf8();
			const uint8_t* json_bytes = (const uint8_t*)json_char.get_data();
			int json_size = json_char.length();
			
			// Send the request
			Error request_err = http_client->request(
				HTTPClient::METHOD_POST, 
				http_request_data.path, 
				http_request_data.headers, 
				json_bytes, 
				json_size
			);
			
			if (request_err != OK) {
				std::cout << "Failed to send request: " << request_err << std::endl;
				waiting_for_response = false;
				set_process(false);
				add_message("System", "Failed to send request: " + String::num_int64(request_err), false);
				http_request_state = REQUEST_NONE;
			} else {
				std::cout << "Request sent successfully" << std::endl;
				http_request_state = REQUEST_REQUESTING;
			}
		} break;
		
		case REQUEST_REQUESTING: {
			// Check if we have a response
			HTTPClient::Status status = http_client->get_status();
			
			if (status == HTTPClient::STATUS_REQUESTING) {
				// Still sending request, keep waiting
				return;
			} else if (status == HTTPClient::STATUS_BODY || status == HTTPClient::STATUS_CONNECTED) {
				// We have a response, move to processing state
				http_request_state = REQUEST_PROCESSING_RESPONSE;
			} else {
				// Request failed
				std::cout << "Request failed with status: " << status << std::endl;
				waiting_for_response = false;
				set_process(false);
				add_message("System", "Request failed. Status: " + String::num_int64(status), false);
				http_request_state = REQUEST_NONE;
			}
		} break;
		
		case REQUEST_PROCESSING_RESPONSE: {
			// Process the response
			if (http_client->has_response()) {
				// Get response headers
				List<String> response_headers;
				http_client->get_response_headers(&response_headers);
				
				// Print response code
				int response_code = http_client->get_response_code();
				std::cout << "Response code: " << response_code << std::endl;
				
				// Print headers
				std::cout << "Response headers:" << std::endl;
				for (const String &header : response_headers) {
					std::cout << "  " << header.utf8().get_data() << std::endl;
				}
				
				// Get response body
				PackedByteArray response_body;
				while (http_client->get_status() == HTTPClient::STATUS_BODY) {
					http_client->poll();
					PackedByteArray chunk = http_client->read_response_body_chunk();
					if (chunk.size() == 0) {
						break;
					} else {
						response_body.append_array(chunk);
					}
				}
				
				// Process response
				String response_text;
				if (response_body.size() > 0) {
					// Use a CharString to convert the byte array to utf8 text
					CharString response_char;
					response_char.resize(response_body.size() + 1);
					memcpy(response_char.ptrw(), response_body.ptr(), response_body.size());
					response_char[response_body.size()] = 0;
					response_text = String::utf8(response_char.ptr());
					
					// Print raw response for debugging
					std::cout << "Raw response: " << response_text.substr(0, 1000).utf8().get_data() << std::endl;
					if (response_text.length() > 1000) {
						std::cout << "... [truncated]" << std::endl;
					}
					
					// Handle the response
					_handle_ai_response(response_text);
				} else {
					// Failed to get response body, use fallback
					add_message("AI Assistant", "I'm having trouble connecting to the server. Please try again later.", true);
				}
				
				// Cleanup
				waiting_for_response = false;
				set_process(false);
				http_client->close();
				http_request_state = REQUEST_NONE;
			}
		} break;
		
		default:
			break;
	}
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

void ChatDock::_handle_ai_response(const String &p_response) {
	// Print the raw response for debugging
	std::cout << "Raw API response received: " << p_response.substr(0, 200).utf8().get_data() << "..." << std::endl;
	
	// Extract the AI response content from the JSON
	String ai_response = p_response;
	
	// Try to extract the actual AI response from the JSON
	JSON json;
	Error json_err = json.parse(p_response);
	
	if (json_err == OK) {
		Variant result = json.get_data();
		
		if (result.get_type() == Variant::DICTIONARY) {
			Dictionary response_json = result;
			
			// Check if response contains result->content structure
			if (response_json.has("result") && response_json["result"].get_type() == Variant::DICTIONARY) {
				Dictionary result_dict = response_json["result"];
				if (result_dict.has("content")) {
					ai_response = result_dict["content"];
					std::cout << "Found AI response in result.content" << std::endl;
				}
			}
		}
	}
	
	// Clear the "Thinking..." message
	chat_display->clear();
	
	// Display the most recent user message
	if (!message_history.is_empty()) {
		add_message("You", message_history[0], false);
	}
	
	// Display the extracted AI response with proper formatting
	add_formatted_ai_response(ai_response);
}

void ChatDock::add_formatted_ai_response(const String &p_message) {
	if (!chat_display) {
		return;
	}

	// Fallback formatting in case of issues
	if (p_message.is_empty()) {
		add_message("AI Assistant", "I received an empty response from the server.", true);
		return;
	}

	try {
		Color ai_color = Color(0.5, 1.0, 0.5);
		Color code_bg_color = Color(0.2, 0.2, 0.2);
		
		// Add sender name with appropriate color
		chat_display->push_color(ai_color);
		chat_display->push_bold();
		chat_display->add_text("AI Assistant:");
		chat_display->pop();
		chat_display->pop();
		chat_display->add_newline();
		
		// Check for any markdown code blocks
		if (p_message.find("```") != -1) {
			// Complex formatting with code blocks
			Vector<String> parts;
			Vector<bool> is_code;
			
			int start = 0;
			bool in_code = false;
			int i = 0;
			
			while (i < p_message.length()) {
				// Look for ```
				if (i + 2 < p_message.length() && 
					p_message[i] == '`' && 
					p_message[i+1] == '`' && 
					p_message[i+2] == '`') {
					
					// Extract the part before this marker
					String part = p_message.substr(start, i - start);
					if (!part.is_empty()) {
						parts.push_back(part);
						is_code.push_back(in_code);
					}
					
					// Skip the ```
					i += 3;
					
					// Skip language identifier if at start of code block
					if (!in_code) {
						int line_start = i;
						// Skip to next newline or another triple backtick
						while (i < p_message.length() && 
							   p_message[i] != '\n' && 
							   !(i + 2 < p_message.length() && 
								 p_message[i] == '`' && 
								 p_message[i+1] == '`' && 
								 p_message[i+2] == '`')) {
							i++;
						}
						
						// Skip the language identifier if we found a newline
						if (i < p_message.length() && p_message[i] == '\n') {
							i++; // Skip the newline
						}
					}
					
					start = i;
					in_code = !in_code;
				} else {
					i++;
				}
			}
			
			// Add the last part
			if (start < p_message.length()) {
				String part = p_message.substr(start);
				if (!part.is_empty()) {
					parts.push_back(part);
					is_code.push_back(in_code);
				}
			}
			
			// Display each part
			for (int j = 0; j < parts.size(); j++) {
				if (parts[j].is_empty()) {
					continue;
				}
				
				if (is_code[j]) {
					// Code block
					chat_display->push_bgcolor(code_bg_color);
					chat_display->push_mono();
					chat_display->push_indent(1);
					
					// Split code by lines to ensure proper formatting
					Vector<String> lines = parts[j].split("\n");
					for (int k = 0; k < lines.size(); k++) {
						chat_display->add_text(lines[k]);
						if (k < lines.size() - 1) {
							chat_display->add_newline();
						}
					}
					
					chat_display->pop(); // indent
					chat_display->pop(); // mono
					chat_display->pop(); // bgcolor
				} else {
					// Regular text
					Vector<String> lines = parts[j].split("\n");
					for (int k = 0; k < lines.size(); k++) {
						chat_display->add_text(lines[k]);
						if (k < lines.size() - 1) {
							chat_display->add_newline();
						}
					}
				}
				
				chat_display->add_newline();
			}
		} else {
			// Simple formatting - no code blocks
			chat_display->add_text(p_message);
		}
		
		// Scroll to bottom
		chat_display->scroll_to_line(chat_display->get_paragraph_count() - 1);
	} catch (...) {
		// In case of any error, fall back to simple message display
		std::cout << "Error in formatting response, using fallback" << std::endl;
		add_message("AI Assistant", p_message, true);
	}
}

ChatDock::ChatDock() {
	set_name("Chat");
	
	// Initialize HTTP client and state
	http_client = HTTPClient::create();
	http_request_state = REQUEST_NONE;
	waiting_for_response = false;
	
	// Chat display area (messages)
	chat_display = memnew(RichTextLabel);
	chat_display->set_v_size_flags(SIZE_EXPAND_FILL);
	chat_display->set_selection_enabled(true);
	chat_display->set_context_menu_enabled(true);
	chat_display->set_focus_mode(FOCUS_NONE);
	chat_display->set_custom_minimum_size(Size2(200, 100));
	chat_display->set_scroll_follow(true);
	add_child(chat_display);
	
	// Add checkbox for including all files
	include_all_files_checkbox = memnew(CheckBox);
	include_all_files_checkbox->set_text("Include All Files");
	include_all_files_checkbox->set_tooltip_text("Include all scene and script files in the project context");
	add_child(include_all_files_checkbox);
	
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
	
	// Setup processing for HTTP requests
	set_process(false);
} 