#include "context_utility.h"

#include "editor/editor_node.h"
#include "editor/editor_file_system.h"
#include "editor/editor_interface.h"
#include "editor/editor_data.h"
#include "editor/editor_settings.h"
#include "scene/main/node.h"
#include "core/io/file_access.h"
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
        static void search_dir(EditorFileSystemDirectory* dir, const Vector<String>& keywords, Vector<String>& paths) {
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
        static void search_dir(EditorFileSystemDirectory* dir, const Vector<String>& keywords, Vector<String>& paths) {
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
    EditorFileSystemDirectory *fs_dir = fs->get_filesystem();
    
    // Use a static function to scan all directories
    struct FileFinder {
        static void scan_dir(EditorFileSystemDirectory* dir, Vector<String>& scenes, Vector<String>& scripts) {
            // Check all files in this directory
            for (int i = 0; i < dir->get_file_count(); i++) {
                String file_name = dir->get_file(i);
                String full_path = dir->get_path() + "/" + file_name;
                
                // Add scenes
                if (file_name.get_extension() == "tscn" || file_name.get_extension() == "scn") {
                    scenes.push_back(full_path);
                }
                
                // Add scripts
                if (file_name.get_extension() == "gd" || file_name.get_extension() == "cs") {
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
}