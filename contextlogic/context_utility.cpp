#include "context_utility.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "core/string/print_string.h"
#include "editor/editor_data.h"
#include "editor/editor_file_system.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_settings.h"
#include "scene/main/node.h"
#include "scene/resources/packed_scene.h"
#include <functional>

String ContextUtility::enrich_prompt(const String &p_prompt, bool p_include_all_files) {
	String enriched_prompt = p_prompt;
	String context = "\n\nContext:\n";
	Vector<String> relevant_scenes;
	Vector<String> relevant_scripts;

	if (p_include_all_files) {
		// Include all scenes and scripts in the project
		_get_all_scenes_and_scripts(relevant_scenes, relevant_scripts);
	} else {
		// Find relevant files based on prompt content
		_find_relevant_scenes(p_prompt, relevant_scenes);
		_find_relevant_scripts(p_prompt, relevant_scripts);
	}

	// Add open scenes
	PackedStringArray open_scenes = EditorInterface::get_singleton()->get_open_scenes();
	for (int i = 0; i < open_scenes.size(); i++) {
		String scene_path = open_scenes[i];
		if (!relevant_scenes.has(scene_path)) {
			relevant_scenes.push_back(scene_path);
		}
	}

	// Add currently edited scene and selection
	Node *edited_scene = EditorInterface::get_singleton()->get_edited_scene_root();
	if (edited_scene) {
		context += "\nCurrently Edited Scene: " + edited_scene->get_scene_file_path() + "\n";

		// Add selected nodes
		Vector<String> selected_node_data;
		_include_selected_nodes(selected_node_data);
		for (int i = 0; i < selected_node_data.size(); i++) {
			context += selected_node_data[i] + "\n";
		}
	}

	// Add scene content
	for (int i = 0; i < relevant_scenes.size(); i++) {
		String scene_path = relevant_scenes[i];
		context += "\nScene: " + scene_path + "\n";
		context += _read_file_content(scene_path) + "\n";
	}

	// Add script content
	for (int i = 0; i < relevant_scripts.size(); i++) {
		String script_path = relevant_scripts[i];
		context += "\nScript: " + script_path + "\n";
		context += _read_file_content(script_path) + "\n";
	}

	return enriched_prompt + context;
}

void ContextUtility::_find_relevant_scenes(const String &p_prompt, Vector<String> &r_paths) {
	// Simple keyword matching for scene names in the prompt
	EditorFileSystem *fs = EditorFileSystem::get_singleton();
	EditorFileSystemDirectory *fs_dir = fs->get_filesystem();

	// Split prompt into words for matching
	Vector<String> words = p_prompt.split(" ", false);

	// Define a recursive function to search directories
	struct SceneSearcher {
		static void search_dir(EditorFileSystemDirectory *dir, const Vector<String> &keywords, Vector<String> &paths) {
			// Check all files in this directory
			for (int i = 0; i < dir->get_file_count(); i++) {
				String file_name = dir->get_file(i);
				if (file_name.get_extension() == "tscn" || file_name.get_extension() == "scn") {
					// Remove extension for matching
					String base_name = file_name.get_basename().to_lower();

					// Check if any keyword matches the file name
					for (int j = 0; j < keywords.size(); j++) {
						if (base_name.find(keywords[j].to_lower()) != -1) {
							String full_path = dir->get_path() + "/" + file_name;
							paths.push_back(full_path);
							break;
						}
					}
				}
			}

			// Recursively check subdirectories
			for (int i = 0; i < dir->get_subdir_count(); i++) {
				search_dir(dir->get_subdir(i), keywords, paths);
			}
		}
	};

	SceneSearcher::search_dir(fs_dir, words, r_paths);
}

void ContextUtility::_find_relevant_scripts(const String &p_prompt, Vector<String> &r_paths) {
	// Similar to scene search but for script files
	EditorFileSystem *fs = EditorFileSystem::get_singleton();
	EditorFileSystemDirectory *fs_dir = fs->get_filesystem();

	Vector<String> words = p_prompt.split(" ", false);

	struct ScriptSearcher {
		static void search_dir(EditorFileSystemDirectory *dir, const Vector<String> &keywords, Vector<String> &paths) {
			for (int i = 0; i < dir->get_file_count(); i++) {
				String file_name = dir->get_file(i);
				if (file_name.get_extension() == "gd" || file_name.get_extension() == "cs") {
					String base_name = file_name.get_basename().to_lower();

					for (int j = 0; j < keywords.size(); j++) {
						if (base_name.find(keywords[j].to_lower()) != -1) {
							String full_path = dir->get_path() + "/" + file_name;
							paths.push_back(full_path);
							break;
						}
					}
				}
			}

			for (int i = 0; i < dir->get_subdir_count(); i++) {
				search_dir(dir->get_subdir(i), keywords, paths);
			}
		}
	};

	ScriptSearcher::search_dir(fs_dir, words, r_paths);
}

void ContextUtility::_include_selected_nodes(Vector<String> &r_context) {
	EditorSelection *selection = EditorInterface::get_singleton()->get_selection();
	TypedArray<Node> selected_nodes = selection->get_selected_nodes();

	for (int i = 0; i < selected_nodes.size(); i++) {
		Node *node = Object::cast_to<Node>(selected_nodes[i]);
		if (node) {
			String node_info = "Selected Node: " + node->get_name() + " (Path: " + node->get_path() + ")";

			// Add script info if attached
			Ref<Script> script = node->get_script();
			if (script.is_valid()) {
				String script_path = script->get_path();
				node_info += " Script: " + script_path;

				// Add script to context if it contains relevant code
				if (!script_path.is_empty()) {
					r_context.push_back(_read_file_content(script_path));
				}
			}

			r_context.push_back(node_info);
		}
	}
}

String ContextUtility::_read_file_content(const String &p_path) {
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::READ, &err);

	if (err != OK) {
		return "Error: Could not open file " + p_path;
	}

	String content = f->get_as_text();

	// Limit content size if needed
	const int MAX_CONTENT_SIZE = 8000; // Adjust as needed
	if (content.length() > MAX_CONTENT_SIZE) {
		content = content.substr(0, MAX_CONTENT_SIZE) + "\n[Content truncated due to size...]";
	}

	return content;
}

void ContextUtility::_get_all_scenes_and_scripts(Vector<String> &r_scenes, Vector<String> &r_scripts) {
	EditorFileSystem *fs = EditorFileSystem::get_singleton();
	if (!fs) {
		print_line("Error: EditorFileSystem singleton is null");
		return;
	}

	EditorFileSystemDirectory *fs_dir = fs->get_filesystem();
	if (!fs_dir) {
		print_line("Error: Root filesystem directory is null");
		return;
	}

	print_line("Starting file system scan from root: " + fs_dir->get_path());

	// Use a static function to scan all directories
	struct FileFinder {
		static void scan_dir(EditorFileSystemDirectory *dir, Vector<String> &scenes, Vector<String> &scripts) {
			if (!dir) {
				return;
			}

			print_line("Scanning directory: " + dir->get_path());

			// Check all files in this directory
			for (int i = 0; i < dir->get_file_count(); i++) {
				String file_name = dir->get_file(i);
				String full_path = dir->get_path().path_join(file_name);

				// Make sure the path is properly formatted
				if (full_path.begins_with("//")) {
					full_path = full_path.substr(1); // Remove one slash if there are two
				}

				// Ensure it starts with res://
				if (!full_path.begins_with("res://")) {
					if (full_path.begins_with("/")) {
						full_path = "res:" + full_path;
					} else {
						full_path = "res://" + full_path;
					}
				}

				print_line("Found file: " + full_path);

				// Add scenes
				if (file_name.get_extension() == "tscn" || file_name.get_extension() == "scn") {
					print_line("Adding scene: " + full_path);
					scenes.push_back(full_path);
				}

				// Add scripts
				if (file_name.get_extension() == "gd" || file_name.get_extension() == "cs") {
					print_line("Adding script: " + full_path);
					scripts.push_back(full_path);
				}
			}

			// Recursively scan subdirectories
			for (int i = 0; i < dir->get_subdir_count(); i++) {
				scan_dir(dir->get_subdir(i), scenes, scripts);
			}
		}
	};

	FileFinder::scan_dir(fs_dir, r_scenes, r_scripts);

	// Report results
	print_line("File scan complete. Found " + itos(r_scenes.size()) + " scenes and " + itos(r_scripts.size()) + " scripts");
}

// Project indexing functions

String ContextUtility::index_project() {
	Vector<String> scenes;
	Vector<String> scripts;

	// Get all scenes and scripts
	_get_all_scenes_and_scripts(scenes, scripts);

	// Debug output
	Array args;
	args.push_back(scenes.size());
	args.push_back(scripts.size());
	print_line(String("Found {0} scenes and {1} scripts").format(args));

	// Create master index
	Dictionary project_index;

	// Project information
	project_index["project_name"] = ProjectSettings::get_singleton()->get("application/config/name");
	project_index["project_path"] = ProjectSettings::get_singleton()->get_resource_path();

	// Debug output - project info
	print_line("Project name: " + String(project_index["project_name"]));
	print_line("Project path: " + String(project_index["project_path"]));

	// Index scripts
	Array scripts_array;
	for (int i = 0; i < scripts.size(); i++) {
		print_line("Processing script: " + scripts[i]);
		Dictionary script_data = _create_script_index(scripts[i]);
		if (script_data.size() > 0) {
			scripts_array.push_back(script_data);
		} else {
			print_line("Warning: Empty data for script " + scripts[i]);
		}
	}
	project_index["scripts"] = scripts_array;

	// Index scenes
	Array scenes_array;
	for (int i = 0; i < scenes.size(); i++) {
		print_line("Processing scene: " + scenes[i]);
		Dictionary scene_data = _create_scene_index(scenes[i]);
		if (scene_data.size() > 0) {
			scenes_array.push_back(scene_data);
		} else {
			print_line("Warning: Empty data for scene " + scenes[i]);
		}
	}
	project_index["scenes"] = scenes_array;

	// Add metadata
	Dictionary metadata;
	metadata["total_files"] = scenes.size() + scripts.size();
	metadata["total_scripts"] = scripts.size();
	metadata["total_scenes"] = scenes.size();

	// Current date and time using Time class
	Dictionary date_time = Time::get_singleton()->get_datetime_dict_from_system();
	String datetime = itos(date_time["year"]) + "-" +
			itos((int)date_time["month"]).pad_zeros(2) + "-" +
			itos((int)date_time["day"]).pad_zeros(2) + "T" +
			itos((int)date_time["hour"]).pad_zeros(2) + ":" +
			itos((int)date_time["minute"]).pad_zeros(2) + ":" +
			itos((int)date_time["second"]).pad_zeros(2) + "Z";
	metadata["created_at"] = datetime;
	metadata["indexed_by"] = "polyz.ai v0.1";

	project_index["metadata"] = metadata;

	// Convert to JSON string using JSON utility class
	JSON json;
	Error err = json.parse("{}"); // Initialize with empty JSON
	if (err != OK) {
		print_line("Error initializing JSON parser");
		return "{}";
	}

	String json_str = JSON::stringify(project_index, "\t");
	print_line("Generated JSON with " + itos(json_str.length()) + " characters");
	return json_str;
}

Dictionary ContextUtility::_create_script_index(const String &p_script_path) {
	Dictionary script_data;
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_script_path, FileAccess::READ, &err);

	if (err != OK) {
		return script_data; // Return empty dictionary if file can't be opened
	}

	script_data["file_path"] = p_script_path;

	// Read file content
	String content = f->get_as_text();
	int line_count = content.split("\n").size();
	script_data["lines_of_code"] = line_count;

	// Parse basic script information
	String class_name = "";
	String extends_class = "";
	Array functions;
	String content_summary = "";

	Vector<String> lines = content.split("\n");

	// Basic parsing for GDScript and C# files
	bool is_gdscript = p_script_path.ends_with(".gd");
	bool is_csharp = p_script_path.ends_with(".cs");

	if (is_gdscript) {
		// For GDScript
		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i].strip_edges();

			// Extract class_name
			if (line.begins_with("class_name")) {
				Vector<String> parts = line.split(" ", false, 2);
				if (parts.size() >= 2) {
					class_name = parts[1].strip_edges();
					// Remove any trailing commas, colons, extends, etc.
					if (class_name.find(":") != -1) {
						class_name = class_name.substr(0, class_name.find(":"));
					}
					if (class_name.find(",") != -1) {
						class_name = class_name.substr(0, class_name.find(","));
					}
				}
			}

			// Extract extends
			if (line.begins_with("extends")) {
				Vector<String> parts = line.split(" ", false, 2);
				if (parts.size() >= 2) {
					extends_class = parts[1].strip_edges();
				}
			}

			// Extract function names
			if (line.begins_with("func ")) {
				Vector<String> parts = line.split("(", false, 2);
				if (parts.size() >= 1) {
					String func_name = parts[0].substr(5).strip_edges(); // Remove "func " prefix
					functions.push_back(func_name);
				}
			}
		}

		// Generate a basic content summary
		content_summary = "GDScript";
		if (!extends_class.is_empty()) {
			content_summary += " extending " + extends_class;
		}
		if (functions.size() > 0) {
			content_summary += " with " + String::num_int64(functions.size()) + " functions";
		}

	} else if (is_csharp) {
		// For C# scripts
		String namespace_name = "";

		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i].strip_edges();

			// Extract namespace
			if (line.begins_with("namespace ")) {
				Vector<String> parts = line.split(" ", false, 2);
				if (parts.size() >= 2) {
					namespace_name = parts[1].strip_edges();
					// Remove any trailing braces
					if (namespace_name.find("{") != -1) {
						namespace_name = namespace_name.substr(0, namespace_name.find("{")).strip_edges();
					}
				}
			}

			// Extract class
			if (line.find("class ") != -1 && line.find(":") != -1) {
				int class_pos = line.find("class ") + 6;
				int extends_pos = line.find(":", class_pos);

				if (class_pos != -1 && extends_pos != -1) {
					class_name = line.substr(class_pos, extends_pos - class_pos).strip_edges();
					String remainder = line.substr(extends_pos + 1).strip_edges();
					Vector<String> inheritance = remainder.split(",", false);
					if (inheritance.size() > 0) {
						extends_class = inheritance[0].strip_edges();
					}
				}
			}

			// Extract method names
			if ((line.find("void ") != -1 || line.find("async ") != -1 ||
						line.find("int ") != -1 || line.find("float ") != -1 ||
						line.find("string ") != -1 || line.find("bool ") != -1) &&
					line.find("(") != -1 && !line.begins_with("//")) {
				int paren_pos = line.find("(");
				String method_part = line.substr(0, paren_pos).strip_edges();
				Vector<String> method_parts = method_part.split(" ", false);

				if (method_parts.size() >= 2) {
					String method_name = method_parts[method_parts.size() - 1];
					functions.push_back(method_name);
				}
			}
		}

		// Generate a basic content summary
		content_summary = "C# script";
		if (!namespace_name.is_empty()) {
			content_summary += " in namespace " + namespace_name;
		}
		if (!extends_class.is_empty()) {
			content_summary += " inheriting from " + extends_class;
		}
		if (functions.size() > 0) {
			content_summary += " with " + String::num_int64(functions.size()) + " methods";
		}
	}

	// Add the data to the dictionary
	if (!class_name.is_empty()) {
		script_data["class_name"] = class_name;
	} else {
		// Use the filename as class name if not specified
		script_data["class_name"] = p_script_path.get_file().get_basename();
	}

	if (!extends_class.is_empty()) {
		script_data["extends"] = extends_class;
	}

	if (functions.size() > 0) {
		script_data["functions"] = functions;
	}

	script_data["content_summary"] = content_summary;

	return script_data;
}

Dictionary ContextUtility::_create_scene_index(const String &p_scene_path) {
	Dictionary scene_data;
	scene_data["file_path"] = p_scene_path;

	// Read scene file to count lines
	Error err;
	Ref<FileAccess> f = FileAccess::open(p_scene_path, FileAccess::READ, &err);

	if (err != OK) {
		return scene_data; // Return with just the path if file can't be opened
	}

	String content = f->get_as_text();
	scene_data["lines_of_code"] = content.split("\n").size();

	// Try to load the scene to extract node information
	Ref<PackedScene> packed_scene = ResourceLoader::load(p_scene_path);
	if (packed_scene.is_valid()) {
		Array nodes_array;

		// Use a safer approach to handle nodes
		// We'll manually parse the scene file instead of instantiating it
		// since instantiation might cause issues in editor context

		// Basic node structure parsing from file
		Vector<String> lines = content.split("\n");
		Dictionary current_node;

		for (int i = 0; i < lines.size(); i++) {
			String line = lines[i].strip_edges();

			// Node name and type detection
			if (line.begins_with("[node name=")) {
				if (current_node.size() > 0) {
					nodes_array.push_back(current_node);
				}

				current_node = Dictionary();

				// Extract name
				int name_start = line.find("\"") + 1;
				int name_end = line.find("\"", name_start);
				if (name_start != -1 && name_end != -1) {
					current_node["name"] = line.substr(name_start, name_end - name_start);
				}

				// Extract type
				int type_start = line.find("type=\"") + 6;
				int type_end = line.find("\"", type_start);
				if (type_start != -1 && type_end != -1) {
					current_node["type"] = line.substr(type_start, type_end - type_start);
				}
			}

			// Script attachment detection
			else if (line.begins_with("script = ExtResource") && current_node.size() > 0) {
				int script_path_start = line.find("path=\"") + 6;
				int script_path_end = line.find("\"", script_path_start);
				if (script_path_start != -1 && script_path_end != -1) {
					String script_path = line.substr(script_path_start, script_path_end - script_path_start);
					current_node["script_attached"] = script_path;
				}
			}
		}

		// Add the last node if any
		if (current_node.size() > 0) {
			nodes_array.push_back(current_node);
		}

		if (nodes_array.size() > 0) {
			scene_data["nodes"] = nodes_array;
		}
	}

	return scene_data;
}