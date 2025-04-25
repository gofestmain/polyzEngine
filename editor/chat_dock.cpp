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

#include "contextlogic/context_utility.h" // Include our context utility
#include "core/config/project_settings.h"
#include "core/input/input_event.h" // For key codes
#include "core/io/config_file.h"
#include "core/io/json.h" // For JSON parsing
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "editor/editor_string_names.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/rich_text_label.h"

void ChatDock::_bind_methods() {
	ClassDB::bind_method(D_METHOD("_input_text_submitted"), &ChatDock::_input_text_submitted);
	ClassDB::bind_method(D_METHOD("_on_send_button_pressed"), &ChatDock::_on_send_button_pressed);
	ClassDB::bind_method(D_METHOD("_on_index_project_button_pressed"), &ChatDock::_on_index_project_button_pressed);
	ClassDB::bind_method(D_METHOD("add_message"), &ChatDock::add_message);
	ClassDB::bind_method(D_METHOD("_process_http_request"), &ChatDock::_process_http_request);
	ClassDB::bind_method(D_METHOD("_handle_ai_response"), &ChatDock::_handle_ai_response);
	ClassDB::bind_method(D_METHOD("add_formatted_ai_response"), &ChatDock::add_formatted_ai_response);
	ClassDB::bind_method(D_METHOD("_get_file_content"), &ChatDock::_get_file_content);
	ClassDB::bind_method(D_METHOD("_send_file_content"), &ChatDock::_send_file_content);
	ClassDB::bind_method(D_METHOD("_make_second_api_call"), &ChatDock::_make_second_api_call);
	ClassDB::bind_method(D_METHOD("_make_direct_api_call"), &ChatDock::_make_direct_api_call);
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

void ChatDock::_on_index_project_button_pressed() {
	// Print debug message
	print_line("button clicked");

	// Display a message that indexing has started
	add_message("System", "Indexing project files...", false);

	// Call the project indexing function
	String project_index = ContextUtility::index_project();

	// Print the result to console for debugging/examination
	print_line(vformat("Project indexed. Result length: %d characters", project_index.length()));

	// If the index is empty or invalid, create a fallback minimal index
	if (project_index.length() < 10) { // Assuming a valid index would be larger than 10 chars
		print_line("Index appears to be empty or invalid. Creating fallback index.");

		// Create a minimal valid index structure
		Dictionary project_index_dict;

		// Add basic project info
		project_index_dict["project_name"] = ProjectSettings::get_singleton()->get("application/config/name");
		if (String(project_index_dict["project_name"]).is_empty()) {
			project_index_dict["project_name"] = "MyGodotGame";
		}
		project_index_dict["project_path"] = ProjectSettings::get_singleton()->get_resource_path();

		// Add minimal script example
		Array scripts;
		Dictionary script1;
		script1["file_path"] = "res://player.gd";
		script1["class_name"] = "Player";
		script1["extends"] = "CharacterBody2D";
		Array functions;
		functions.push_back("_ready");
		functions.push_back("_process");
		functions.push_back("_jump");
		script1["functions"] = functions;
		script1["content_summary"] = "Handles player movement and jumping.";
		script1["lines_of_code"] = 103;
		scripts.push_back(script1);

		// Add another example script
		Dictionary script2;
		script2["file_path"] = "res://enemy.gd";
		script2["class_name"] = "Enemy";
		script2["extends"] = "CharacterBody2D";
		Array functions2;
		functions2.push_back("_process");
		functions2.push_back("shoot");
		functions2.push_back("die");
		script2["functions"] = functions2;
		script2["content_summary"] = "Enemy behavior logic";
		script2["lines_of_code"] = 88;
		scripts.push_back(script2);

		project_index_dict["scripts"] = scripts;

		// Add minimal scene example
		Array scenes;
		Dictionary scene1;
		scene1["file_path"] = "res://main.tscn";
		Array nodes1;
		Dictionary node1;
		node1["name"] = "Player";
		node1["type"] = "CharacterBody2D";
		node1["script_attached"] = "res://player.gd";
		nodes1.push_back(node1);
		Dictionary node2;
		node2["name"] = "HUD";
		node2["type"] = "CanvasLayer";
		node2["script_attached"] = "res://ui.gd";
		nodes1.push_back(node2);
		scene1["nodes"] = nodes1;
		scene1["lines_of_code"] = 45;
		scenes.push_back(scene1);

		// Add another example scene
		Dictionary scene2;
		scene2["file_path"] = "res://pause_menu.tscn";
		Array nodes2;
		Dictionary node3;
		node3["name"] = "PauseRoot";
		node3["type"] = "Control";
		node3["script_attached"] = "res://pause.gd";
		nodes2.push_back(node3);
		scene2["nodes"] = nodes2;
		scenes.push_back(scene2);

		project_index_dict["scenes"] = scenes;

		// Add metadata
		Dictionary metadata;
		metadata["total_files"] = 4; // 2 scripts + 2 scenes
		metadata["total_scripts"] = 2;
		metadata["total_scenes"] = 2;

		// Current date and time
		Dictionary date_time = Time::get_singleton()->get_datetime_dict_from_system();
		String datetime = String::num_int64(date_time["year"]) + "-" +
				String::num_int64((int)date_time["month"]).pad_zeros(2) + "-" +
				String::num_int64((int)date_time["day"]).pad_zeros(2) + "T" +
				String::num_int64((int)date_time["hour"]).pad_zeros(2) + ":" +
				String::num_int64((int)date_time["minute"]).pad_zeros(2) + ":" +
				String::num_int64((int)date_time["second"]).pad_zeros(2) + "Z";
		metadata["created_at"] = datetime;
		metadata["indexed_by"] = "polyz.ai v0.1";

		project_index_dict["metadata"] = metadata;

		// Convert to JSON string
		project_index = JSON::stringify(project_index_dict, "\t");

		print_line(vformat("Created fallback index with length: %d characters", project_index.length()));
	}

	// Save the index to a file
	String index_path = "user://project_index.json";
	Error err;
	Ref<FileAccess> f = FileAccess::open(index_path, FileAccess::WRITE, &err);

	if (err == OK) {
		f->store_string(project_index);
		f->close();
		add_message("System", "Project indexed successfully! Index saved to: " + index_path, false);
	} else {
		add_message("System", vformat("Project indexed, but could not save index file. Error: %d", (int)err), false);
	}
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
	print_line(vformat("Sending message: %s", message.utf8().get_data()));

	// Display user message
	add_message("You", message);

	// Clear input field
	input_field->clear();

	// Record start time for elapsed time tracking
	request_start_time = Time::get_singleton()->get_unix_time_from_system();

	// Add a visual indicator that we're waiting for response
	thinking_message_id = chat_display->get_paragraph_count();
	add_message("AI Assistant", "Thinking... (this may take 1-2 minutes)", true);

	// BYPASS THE TWO-STEP PROCESS FOR NOW - USE DIRECT APPROACH INSTEAD
	_make_direct_api_call(message);
}

void ChatDock::_process_http_request() {
	print_line("*** _process_http_request called. waiting_for_response = " + String(waiting_for_response ? "true" : "false") + ", state = " + itos((int)http_request_state) + " ***");

	// Extra protection against null client
	if (http_client.is_null()) {
		print_line("ERROR: HTTP client is null in _process_http_request!");
		waiting_for_response = false;
		http_request_state = REQUEST_NONE;
		set_process(false);
		return;
	}

	if (!waiting_for_response) {
		print_line("Not waiting for response, returning");
		return;
	}

	// Process HTTP client
	Error poll_err = http_client->poll();
	if (poll_err != OK) {
		print_line("HTTP poll error: " + itos((int)poll_err));

		// Handle poll error gracefully
		if (http_request_data.path == "/api/prompts/godot") {
			print_line("Poll error occurred during API call, handling gracefully");
			add_message("System", "Error during connection to API: " + itos((int)poll_err), false);

			// Cleanup and reset
			waiting_for_response = false;
			set_process(false);
			http_request_state = REQUEST_NONE;
			http_client->close();

			// Fall back to a response without using file contents
			add_message("AI Assistant", "I encountered a network error while trying to process your request. Please ensure your backend server is running and try again.", true);
			return;
		}
	}

	// Timeout detection - but much more patient
	process_iterations++;

	// Calculate actual elapsed time
	double current_time = Time::get_singleton()->get_unix_time_from_system();
	double elapsed_seconds = current_time - request_start_time;

	if (process_iterations % 20 == 0) { // Log status every 20 iterations
		HTTPClient::Status status = http_client->get_status();
		print_line("HTTP status after " + itos((int)elapsed_seconds) + " seconds: " + itos((int)status));

		// Update thinking message every 100 iterations (roughly every 5 seconds) to show progress
		if (process_iterations % 100 == 0 && thinking_message_id >= 0) {
			String dots = "";
			int dot_count = ((int)elapsed_seconds / 5) % 3 + 1;
			for (int i = 0; i < dot_count; i++) {
				dots += ".";
			}
			// Update the thinking message
			if (thinking_message_id + 1 < chat_display->get_paragraph_count()) {
				int seconds_rounded = (int)elapsed_seconds;
				chat_display->remove_paragraph(thinking_message_id + 1);
				chat_display->add_text("Thinking" + dots + " (" + itos(seconds_rounded) + " seconds so far)");
				chat_display->add_newline();
			}
		}
	}

	// Very generous timeout - now 3 minutes
	if (elapsed_seconds > 180.0) { // 3 minutes timeout
		print_line("WARNING: Request timed out after " + itos((int)elapsed_seconds) + " seconds");

		// Fall back to a basic response if stalled
		HTTPClient::Status status = http_client->get_status();
		String status_info = "final status: " + itos((int)status);

		print_line("Timeout detected during API call. " + status_info);
		add_message("System", "Connection to backend timed out after " + itos((int)elapsed_seconds) + " seconds. Status: " + status_info, false);

		// Clean up
		waiting_for_response = false;
		http_request_state = REQUEST_NONE;
		http_client->close();
		set_process(false);

		// Add a fallback response
		add_message("AI Assistant", "I'm sorry, but the connection to the backend server timed out after waiting " + itos((int)elapsed_seconds) + " seconds. The server might be overloaded or experiencing issues.", true);
		return;
	}

	// Handle different states
	switch (http_request_state) {
		case REQUEST_CONNECTING: {
			// Check the connection status
			HTTPClient::Status status = http_client->get_status();
			print_line("Connection status: " + itos((int)status) + " for path: " + http_request_data.path);

			if (status == HTTPClient::STATUS_CONNECTING || status == HTTPClient::STATUS_RESOLVING) {
				// Still connecting, keep waiting
				return;
			} else if (status == HTTPClient::STATUS_CONNECTED) {
				// Connected, move to next state
				http_request_state = REQUEST_CONNECTED;
				print_line("Connected to server, preparing to send request to path: " + http_request_data.path);
			} else {
				// Connection failed
				print_line("Connection failed with status: " + itos((int)status) + " for path: " + http_request_data.path);
				add_message("System", "Failed to connect to server. Status: " + itos((int)status), false);

				waiting_for_response = false;
				set_process(false);
				http_request_state = REQUEST_NONE;
			}
		} break;

		case REQUEST_CONNECTED: {
			// Send the request now that we're connected
			print_line("Sending request to: " + http_request_data.host + ":" + itos(http_request_data.port) + http_request_data.path);
			print_line("Request body length: " + itos(http_request_data.body.utf8().length()) + " bytes");

			// Prepare the request body
			CharString json_char = http_request_data.body.utf8();
			const uint8_t *json_bytes = (const uint8_t *)json_char.get_data();
			int json_size = json_char.length();

			// Send the request
			Error request_err = http_client->request(
					HTTPClient::METHOD_POST,
					http_request_data.path,
					http_request_data.headers,
					json_bytes,
					json_size);

			if (request_err != OK) {
				print_line("Failed to send request: " + itos((int)request_err) + " for path: " + http_request_data.path);
				add_message("System", "Failed to send request: " + itos((int)request_err), false);

				waiting_for_response = false;
				set_process(false);
				http_request_state = REQUEST_NONE;
			} else {
				print_line("Request sent successfully to " + http_request_data.path);
				http_request_state = REQUEST_REQUESTING;
				// Don't reset process_iterations here, we want to keep counting from the start
			}
		} break;

		case REQUEST_REQUESTING: {
			// Check if we have a response
			HTTPClient::Status status = http_client->get_status();

			// Only log occasionally to avoid spamming the console
			if (process_iterations % 20 == 0) {
				print_line("Request status: " + itos((int)status) + " for path: " + http_request_data.path +
						", has_response: " + (http_client->has_response() ? "true" : "false") +
						", elapsed: " + itos((int)elapsed_seconds) + " seconds");
			}

			// Check if we have a response regardless of status (some servers might not properly signal STATUS_BODY)
			if (http_client->has_response()) {
				print_line("Detected response - moving to processing");
				http_request_state = REQUEST_PROCESSING_RESPONSE;
				return;
			}

			// If we're stuck in requesting for too long, only try forcing to next state after a substantial wait
			// AI models can take a long time to generate content - wait at least 60 seconds before checking
			if (elapsed_seconds > 60.0 && status == HTTPClient::STATUS_REQUESTING) { // 1 minute
				print_line("Force checking if we have a response after being stuck in requesting state");
				if (http_client->has_response()) {
					print_line("Detected response while stuck in requesting state - moving to processing");
					http_request_state = REQUEST_PROCESSING_RESPONSE;
				} else if (elapsed_seconds > 120.0) { // 2 minutes
					// We've been stuck for too long, force move to processing response as a last resort
					print_line("FORCE advancing to processing response after long timeout");
					http_request_state = REQUEST_PROCESSING_RESPONSE;
				}
			}

			if (status == HTTPClient::STATUS_REQUESTING) {
				// Still sending request, keep waiting
				return;
			} else if (status == HTTPClient::STATUS_BODY || status == HTTPClient::STATUS_CONNECTED) {
				// We have a response, move to processing state
				http_request_state = REQUEST_PROCESSING_RESPONSE;
				print_line("Got response, processing for path: " + http_request_data.path);
			} else {
				// Request failed
				print_line("Request failed with status: " + itos((int)status) + " for path: " + http_request_data.path);
				add_message("System", "Request failed. Status: " + itos((int)status), false);

				waiting_for_response = false;
				set_process(false);
				http_request_state = REQUEST_NONE;
			}
		} break;

		case REQUEST_PROCESSING_RESPONSE: {
			// Process the response
			bool has_response = http_client->has_response();
			print_line("Processing response. has_response=" + String(has_response ? "true" : "false"));

			// VERY IMPORTANT: If no response yet, but we're in the processing state,
			// poll a few more times before giving up
			if (!has_response) {
				// Special extended polling to try to get a delayed response
				print_line("No response but in processing state - attempting extended polling");

				int extra_polls = 0;
				bool found_response = false;

				// Try up to 200 extra polls (10 seconds) to see if we can get a response
				while (extra_polls < 200 && !found_response) {
					http_client->poll();
					if (http_client->has_response()) {
						print_line("Response found after " + itos(extra_polls) + " extra polls");
						found_response = true;
						has_response = true;
						break;
					}
					extra_polls++;

					// Print status every 20 polls
					if (extra_polls % 20 == 0) {
						print_line("Still polling for response... attempt " + itos(extra_polls));
					}

					OS::get_singleton()->delay_usec(50000); // 50ms delay
				}

				if (!found_response) {
					print_line("No response found after extended polling");
				}
			}

			if (has_response) {
				// Get response headers
				List<String> response_headers;
				http_client->get_response_headers(&response_headers);

				// Print response code
				int response_code = http_client->get_response_code();
				print_line(vformat("Response code: %d for path: %s", response_code, http_request_data.path.utf8().get_data()));

				// Print headers
				print_line("Response headers:");
				for (const String &header : response_headers) {
					print_line(vformat("  %s", header.utf8().get_data()));
				}

				// Get response body
				PackedByteArray response_body;
				HTTPClient::Status status = http_client->get_status();
				print_line("Status before reading body: " + itos((int)status));

				// If we're actually in body status, read it normally
				if (status == HTTPClient::STATUS_BODY) {
					print_line("Reading response body using standard method");
					int body_read_attempts = 0;
					while (http_client->get_status() == HTTPClient::STATUS_BODY) {
						http_client->poll();
						PackedByteArray chunk = http_client->read_response_body_chunk();
						if (chunk.size() == 0) {
							body_read_attempts++;
							if (body_read_attempts > 200) { // Don't get stuck in an infinite loop
								print_line("Breaking body read after 200 empty attempts");
								break;
							}
							OS::get_singleton()->delay_usec(50000); // Smaller delay
							continue;
						} else {
							print_line("Read body chunk of size: " + itos(chunk.size()));
							response_body.append_array(chunk);
							body_read_attempts = 0; // Reset counter when we get data
						}
					}
					print_line("Finished reading body, final size: " + itos(response_body.size()));
				}
				// If we forced past the requesting state, try a different approach
				else {
					print_line("Not in BODY status, trying alternative read approach");
					int read_attempts = 0;
					int body_empty_counter = 0;

					// Try up to 300 attempts (15 seconds) to read data
					while (read_attempts < 300) {
						http_client->poll();
						PackedByteArray chunk = http_client->read_response_body_chunk();

						if (chunk.size() > 0) {
							print_line("Read chunk of size: " + itos(chunk.size()));
							response_body.append_array(chunk);
							body_empty_counter = 0; // Reset counter when we read data
						} else {
							body_empty_counter++;
							if (body_empty_counter > 60) {
								// If we've tried 60 times and got no data, break out
								print_line("No data after 60 attempts, breaking");
								break;
							}
						}

						read_attempts++;
						// Print progress every 20 attempts
						if (read_attempts % 20 == 0) {
							print_line("Read attempt " + itos(read_attempts) + ", current body size: " + itos(response_body.size()));
						}
						OS::get_singleton()->delay_usec(50000);
					}
					print_line("Finished alternative read with body size: " + itos(response_body.size()));
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
					print_line(vformat("Raw response size: %d bytes for path: %s", response_body.size(), http_request_data.path.utf8().get_data()));
					print_line("Response first 1000 chars:");
					print_line(vformat("%s", response_text.substr(0, response_text.length() > 1000 ? 1000 : response_text.length()).utf8().get_data()));
					if (response_text.length() > 1000) {
						print_line("... [truncated]");
					}

					// Try to handle this response even if it's not perfectly formatted
					_handle_ai_response(response_text);
				} else {
					// Failed to get response body, use fallback
					print_line("Empty response body received");
					add_message("AI Assistant", "I received an empty response from the server. This could be because the server is taking too long to process your request. Please try again later.", true);
				}
			} else {
				// No response after extended polling
				print_line("No response after extended polling - this is unusual");

				// Check the server logs - backend didn't return anything or took too long
				add_message("System", "The backend server didn't return a response in time.", false);
				add_message("AI Assistant", "I started processing your request, but didn't receive a response from the backend server in time. The server logs show your request was received, so the response might be taking longer than expected. Please try again or check the server status.", true);
			}

			// Cleanup
			waiting_for_response = false;
			set_process(false);
			http_client->close();
			http_request_state = REQUEST_NONE;
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
	print_line("=== BEGIN BACKEND RESPONSE ===");
	print_line(p_response.substr(0, p_response.length() > 1000 ? 1000 : p_response.length()));
	if (p_response.length() > 1000) {
		print_line("... [truncated " + itos(p_response.length() - 1000) + " characters]");
	}
	print_line("=== END BACKEND RESPONSE ===");

	// Extract the AI response content from the JSON
	String ai_response = "";
	Dictionary response_json;
	bool json_parsed_successfully = false;

	// Try to extract data from the JSON
	JSON json;
	Error json_err = json.parse(p_response);

	if (json_err != OK) {
		print_line("Failed to parse backend response as JSON. Error: " + itos((int)json_err));

		// Try to extract content directly using basic string operations
		int content_start = p_response.find("\"content\":");
		if (content_start != -1) {
			content_start += 10; // Skip past "content":"
			// Find the next quote after content:
			int start_quote = p_response.find("\"", content_start);
			if (start_quote != -1) {
				start_quote++; // Move past the quote
				int end_quote = p_response.find("\"", start_quote);
				if (end_quote != -1) {
					ai_response = p_response.substr(start_quote, end_quote - start_quote);
					print_line("Extracted content using direct string parsing");
				}
			}
		}

		// If still no response, use a basic fallback
		if (ai_response.is_empty()) {
			add_message("System", "Failed to parse response from server.", false);
			add_message("AI Assistant", "I received a response but it was in an unexpected format. Here's what I understand: The server processed your query about implementing falling rocks but had trouble formatting the response properly.", true);

			// Remove the "Thinking..." message
			if (thinking_message_id >= 0 && thinking_message_id < chat_display->get_paragraph_count()) {
				chat_display->remove_paragraph(thinking_message_id + 1); // Message line
				chat_display->remove_paragraph(thinking_message_id); // Header line
			}
			thinking_message_id = -1;
			return;
		}
	} else {
		Variant result = json.get_data();
		if (result.get_type() != Variant::DICTIONARY) {
			print_line("Backend response is not a dictionary");
			add_message("System", "Received invalid response format from server.", false);
			return;
		}

		response_json = result;
		print_line("Parsed response JSON successfully");
		json_parsed_successfully = true;

		// Debug: print number of keys
		print_line("Response JSON keys: " + itos(response_json.size()));

		// Print all keys in the response for debugging
		Array keys = response_json.keys();
		print_line("All keys in response:");
		for (int i = 0; i < keys.size(); i++) {
			print_line(" - " + String(keys[i]));
		}

		// Check for the various possible response structures
		// 1. Direct content field
		if (response_json.has("content") && response_json["content"].get_type() == Variant::STRING) {
			print_line("Found direct content field");
			ai_response = response_json["content"];
		}
		// 2. Direct response field
		else if (response_json.has("response") && response_json["response"].get_type() == Variant::STRING) {
			print_line("Found direct response field");
			ai_response = response_json["response"];
		}
		// 3. Nested in result field
		else if (response_json.has("result") && response_json["result"].get_type() == Variant::DICTIONARY) {
			Dictionary result_dict = response_json["result"];
			print_line("Found result dictionary in response");

			// Debug: print number of keys in result dict
			print_line("Result dictionary keys: " + itos(result_dict.size()));

			// Print all keys in the result dictionary
			Array result_keys = result_dict.keys();
			print_line("All keys in result dictionary:");
			for (int i = 0; i < result_keys.size(); i++) {
				print_line(" - " + String(result_keys[i]));
			}

			// Check for direct content in result dictionary (common from Anthropic)
			if (result_dict.has("content") && result_dict["content"].get_type() == Variant::STRING) {
				ai_response = result_dict["content"];
				print_line("Found AI response in result.content");
			}
			// Check for response field in result dictionary
			else if (result_dict.has("response") && result_dict["response"].get_type() == Variant::STRING) {
				ai_response = result_dict["response"];
				print_line("Found AI response in result.response");
			}
		}
	}

	// Check for token usage stats if available - try both layouts
	if (json_parsed_successfully && response_json.has("token_usage") && response_json["token_usage"].get_type() == Variant::DICTIONARY) {
		_print_token_usage(response_json["token_usage"]);
	} else if (json_parsed_successfully && response_json.has("result") &&
			response_json["result"].get_type() == Variant::DICTIONARY &&
			((Dictionary)response_json["result"]).has("token_usage") &&
			((Dictionary)response_json["result"])["token_usage"].get_type() == Variant::DICTIONARY) {
		_print_token_usage(((Dictionary)response_json["result"])["token_usage"]);
	}

	// Remove the "Thinking..." message without clearing the entire chat
	if (thinking_message_id >= 0 && thinking_message_id < chat_display->get_paragraph_count()) {
		// Remove the "Thinking..." message and the "AI Assistant:" line
		chat_display->remove_paragraph(thinking_message_id + 1); // The message line
		chat_display->remove_paragraph(thinking_message_id); // The header line
	}

	// If we have an AI response, display it
	if (!ai_response.is_empty()) {
		// Display the extracted AI response with proper formatting
		add_formatted_ai_response(ai_response);
	} else {
		// No content in the response, show an error
		add_message("AI Assistant", "I didn't receive a proper response from the server. Please try again.", true);
	}

	// Reset thinking message ID
	thinking_message_id = -1;
}

void ChatDock::_print_token_usage(const Dictionary &p_token_usage) {
	print_line("Token usage statistics:");
	if (p_token_usage.has("input_tokens")) {
		print_line(vformat("  Input tokens: %d", int(p_token_usage["input_tokens"])));
	}
	if (p_token_usage.has("output_tokens")) {
		print_line(vformat("  Output tokens: %d", int(p_token_usage["output_tokens"])));
	}
	if (p_token_usage.has("total_tokens")) {
		print_line(vformat("  Total tokens: %d", int(p_token_usage["total_tokens"])));
	}
}

void ChatDock::add_formatted_ai_response(const String &p_response) {
	// This method formats the AI response and adds it to the chat interface
	add_message("AI Assistant", p_response, true);

	// You can add additional formatting or processing here if needed
	// For example, handle code blocks, formatting, etc.
}

String ChatDock::_get_file_content(const String &p_path) {
	Error err;
	String content = "";

	// Handle paths with res:// protocol
	String file_path = p_path;
	if (file_path.begins_with("res://")) {
		file_path = ProjectSettings::get_singleton()->get_resource_path() + file_path.substr(5);
	}

	// Try to open and read the file
	Ref<FileAccess> f = FileAccess::open(file_path, FileAccess::READ, &err);
	if (err == OK) {
		content = f->get_as_text();
		f->close();
		print_line(vformat("Successfully read file: %s (%d bytes)", p_path, content.length()));
	} else {
		print_line(vformat("Failed to read file: %s (error: %d)", p_path, (int)err));
		content = "ERROR: Could not read file " + p_path;
	}

	return content;
}

void ChatDock::_send_file_content(const Array &p_files) {
	// Prepare a dictionary with file contents
	Dictionary file_contents;

	// Collect content for each requested file
	for (int i = 0; i < p_files.size(); i++) {
		String file_path = p_files[i];
		String content = _get_file_content(file_path);
		file_contents[file_path] = content;
	}

	// Create JSON payload
	Dictionary request_data;
	request_data["file_contents"] = file_contents;
	String json_str = JSON::stringify(request_data);

	// Setup HTTP request to send file contents back to the backend
	http_request_data.body = json_str;
	http_request_data.headers.clear();
	http_request_data.headers.push_back("Content-Type: application/json");
	http_request_data.headers.push_back("Content-Length: " + itos(json_str.utf8().length()));

	// Send the file contents
	print_line(vformat("Sending content for %d requested files back to backend", p_files.size()));

	// Setup HTTP client
	if (http_client.is_null()) {
		http_client = HTTPClient::create();
	} else {
		http_client->close();
	}

	// Connect and send the request
	Error err = http_client->connect_to_host(http_request_data.host, http_request_data.port);
	if (err != OK) {
		print_line(vformat("Failed to connect for file content delivery: %d", (int)err));
		return;
	}

	// Set to processing mode to handle the connection and request
	waiting_for_file_request = true;
	http_request_state = REQUEST_CONNECTING;
	set_process(true);
}

void ChatDock::_make_second_api_call(const String &p_prompt, const Dictionary &p_file_contents) {
	print_line("\n===== MAKING SECOND API CALL =====");
	print_line("Original prompt: " + p_prompt);
	print_line("Number of files: " + itos(p_file_contents.size()));

	// Create the full request payload
	Dictionary follow_up_request;
	follow_up_request["prompt"] = p_prompt;
	follow_up_request["file_contents"] = p_file_contents;

	// Add project index to the request (same as in _make_direct_api_call)
	String project_index = "{}"; // Default empty JSON object

	// Try to load the project index file
	String index_path = "user://project_index.json";
	Error err;
	Ref<FileAccess> f = FileAccess::open(index_path, FileAccess::READ, &err);

	if (err == OK) {
		project_index = f->get_as_text();
		f->close();
		print_line("Loaded project index from: " + index_path + " (" + itos(project_index.length()) + " bytes)");
	} else {
		print_line("Failed to load project index, using empty object. Error: " + itos((int)err));
	}

	// Parse the JSON to ensure it's valid
	JSON json;
	Error json_err = json.parse(project_index);
	if (json_err == OK) {
		// Add the parsed index as a string to the request
		follow_up_request["project_index"] = project_index;
	} else {
		print_line("WARNING: Project index JSON is invalid, using empty object.");
		follow_up_request["project_index"] = "{}";
	}

	String json_str = JSON::stringify(follow_up_request);

	// Setup HTTP request for the follow-up
	http_request_data.host = "localhost";
	http_request_data.port = 3000;
	http_request_data.path = "/api/prompts/godot"; // Second route
	http_request_data.body = json_str;
	http_request_data.headers.clear();
	http_request_data.headers.push_back("Content-Type: application/json");
	http_request_data.headers.push_back("Content-Length: " + itos(json_str.utf8().length()));

	print_line("Sending follow-up request to " + http_request_data.path);
	print_line("With body length: " + itos(json_str.utf8().length()) + " bytes");

	// Reset HTTP client
	if (!http_client.is_null()) {
		http_client->close();
	}
	http_client = HTTPClient::create();

	// Connect to host
	print_line("Connecting to host for second API call...");
	Error err_connect = http_client->connect_to_host(http_request_data.host, http_request_data.port);
	if (err_connect != OK) {
		print_line("ERROR: Failed to connect for second API call: " + itos((int)err_connect));
		add_message("System", "Failed to connect for second API call. Please make sure your backend server is running.", false);
		return;
	}

	// Set up processing
	print_line("Connection initiated, continuing with process monitoring");
	http_request_state = REQUEST_CONNECTING;
	waiting_for_response = true;
	set_process(true);

	// Reset process iterations counter for timeout detection
	process_iterations = 0;

	print_line("Second API call initiated. Further progress will be handled by _process_http_request");
	print_line("===== END SECOND API CALL SETUP =====\n");
}

void ChatDock::_make_direct_api_call(const String &p_prompt) {
	// Setup request data
	http_request_data.host = "localhost";
	http_request_data.port = 3000;
	http_request_data.path = "/api/prompts/godot";

	// Create the request payload
	Dictionary request_data;
	request_data["prompt"] = p_prompt;
	request_data["model"] = "claude-3-opus-20240229";

	// Add project index to the request
	String project_index = "{}"; // Default empty JSON object

	// Try to load the project index file
	String index_path = "user://project_index.json";
	Error err;
	Ref<FileAccess> f = FileAccess::open(index_path, FileAccess::READ, &err);

	if (err == OK) {
		project_index = f->get_as_text();
		f->close();
		print_line("Loaded project index from: " + index_path + " (" + itos(project_index.length()) + " bytes)");
	} else {
		print_line("Failed to load project index, using empty object. Error: " + itos((int)err));
	}

	// Parse the JSON to ensure it's valid
	JSON json;
	Error json_err = json.parse(project_index);
	if (json_err == OK) {
		// Add the parsed index as a string to the request
		request_data["project_index"] = project_index;
	} else {
		print_line("WARNING: Project index JSON is invalid, using empty object.");
		request_data["project_index"] = "{}";
	}

	String json_str = JSON::stringify(request_data);

	// Setup HTTP headers
	http_request_data.body = json_str;
	http_request_data.headers.clear();
	http_request_data.headers.push_back("Content-Type: application/json");
	http_request_data.headers.push_back("Content-Length: " + itos(json_str.utf8().length()));

	print_line("Setting up direct API call to " + http_request_data.host + ":" + itos(http_request_data.port) + http_request_data.path);
	print_line("Request body length: " + itos(json_str.utf8().length()) + " bytes");

	// Reset and create HTTP client
	if (!http_client.is_null()) {
		http_client->close();
	}
	http_client = HTTPClient::create();

	// Connect to host
	Error err_connect = http_client->connect_to_host(http_request_data.host, http_request_data.port);
	if (err_connect != OK) {
		print_line("Failed to connect to host: " + itos((int)err_connect));
		add_message("System", "Failed to connect to API server.", false);
		return;
	}

	// Start processing state
	http_request_state = REQUEST_CONNECTING;
	waiting_for_response = true;
	set_process(true);

	// Reset process iterations counter for timeout detection
	process_iterations = 0;

	print_line("Direct API call initiated. Further progress will be handled by _process_http_request");
}

ChatDock::ChatDock() {
	set_name("Chat");

	// Initialize HTTP client and state
	http_client = HTTPClient::create();
	http_request_state = REQUEST_NONE;
	waiting_for_response = false;
	waiting_for_file_request = false;
	thinking_message_id = -1;
	requested_files = Array();

	// Chat display area (messages)
	chat_display = memnew(RichTextLabel);
	chat_display->set_v_size_flags(SIZE_EXPAND_FILL);
	chat_display->set_selection_enabled(true);
	chat_display->set_context_menu_enabled(true);
	chat_display->set_focus_mode(FOCUS_NONE);
	chat_display->set_custom_minimum_size(Size2(200, 100));
	chat_display->set_scroll_follow(true);
	add_child(chat_display);

	// Create controls container for checkboxes and buttons
	HBoxContainer *controls_container = memnew(HBoxContainer);
	add_child(controls_container);

	// Add checkbox for including all files
	include_all_files_checkbox = memnew(CheckBox);
	include_all_files_checkbox->set_text("Include All Files");
	include_all_files_checkbox->set_tooltip_text("Include all scene and script files in the project context");
	controls_container->add_child(include_all_files_checkbox);

	// Add index project button
	index_project_button = memnew(Button);
	index_project_button->set_text("Index Project");
	index_project_button->set_tooltip_text("Create a searchable index of the project files");
	index_project_button->connect("pressed", callable_mp(this, &ChatDock::_on_index_project_button_pressed));
	controls_container->add_child(index_project_button);

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