#ifndef CONTEXT_UTILITY_H
#define CONTEXT_UTILITY_H

#include "core/object/ref_counted.h"
#include "core/templates/vector.h"

class ContextUtility {
public:
	static String enrich_prompt(const String &p_prompt, bool p_include_all_files = false);
	static String index_project();

private:
	static void _find_relevant_scenes(const String &p_prompt, Vector<String> &r_paths);
	static void _find_relevant_scripts(const String &p_prompt, Vector<String> &r_paths);
	static void _include_selected_nodes(Vector<String> &r_context);
	static String _read_file_content(const String &p_path);
	static void _get_all_scenes_and_scripts(Vector<String> &r_scenes, Vector<String> &r_scripts);
	static Dictionary _create_script_index(const String &p_script_path);
	static Dictionary _create_scene_index(const String &p_scene_path);
};

#endif // CONTEXT_UTILITY_H

// In chat_dock.cpp
