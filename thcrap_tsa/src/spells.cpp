/**
  * Touhou Community Reliant Automatic Patcher
  * Team Shanghai Alice support plugin
  *
  * ----
  *
  * Spell card patching via breakpoints.
  */

#include <thcrap.h>
#include "thcrap_tsa.h"

// Lookup cache
static int cache_spell_id = 0;
static int cache_spell_id_real = 0;

int BP_spell_id(x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	json_t *spell_id = json_object_get(bp_info, "spell_id");
	json_t *spell_id_real = json_object_get(bp_info, "spell_id_real");
	json_t *spell_rank = json_object_get(bp_info, "spell_rank");
	// ----------

	if(spell_id) {
		cache_spell_id = json_immediate_value(spell_id, regs);
		cache_spell_id_real = cache_spell_id;
	}
	if(spell_id_real) {
		cache_spell_id_real = json_immediate_value(spell_id_real, regs);
	}
	if(spell_rank) {
		int rank = json_immediate_value(spell_rank, regs);
		cache_spell_id = cache_spell_id_real - rank;
	}
	return 1;
}

int BP_spell_name(x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	const char **spell_name = (const char**)json_object_get_pointer(bp_info, regs, "spell_name");
	// ----------

	// Other breakpoints
	// -----------------
	BP_spell_id(regs, bp_info);
	// -----------------

	if(spell_name && cache_spell_id_real >= cache_spell_id) {
		const char *new_name = NULL;
		int i = cache_spell_id_real;
		json_t *spells = jsondata_game_get("spells.js");

		// Count down from the real number to the given number
		// until we find something
		do {
			new_name = json_string_value(json_object_numkey_get(spells, i));
		} while( (i-- > cache_spell_id) && i >= 0 && !new_name );

		if(new_name) {
			*spell_name = new_name;
			return breakpoint_cave_exec_flag(bp_info);
		}
	}
	return 1;
}

int BP_spell_comment_line(x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	const char **str = (const char**)json_object_get_register(bp_info, regs, "str");
	size_t comment_num = json_object_get_hex(bp_info, "comment_num");
	size_t line_num = json_object_get_hex(bp_info, "line_num");
	// ----------

	// Other breakpoints
	// -----------------
	BP_spell_id(regs, bp_info);
	// -----------------

	if(str && comment_num) {
		json_t *json_cmt = NULL;
		int i = cache_spell_id_real;
		json_t *spellcomments = jsondata_game_get("spellcomments.js");

		size_t cmt_key_str_len = strlen("comment_") + 16 + 1;
		VLA(char, cmt_key_str, cmt_key_str_len);
		defer({ VLA_FREE(cmt_key_str); });
		sprintf(cmt_key_str, "comment_%u", comment_num);

		// Count down from the real number to the given number
		// until we find something
		do {
			json_cmt = json_object_numkey_get(spellcomments, i);
			json_cmt = json_object_get(json_cmt, cmt_key_str);
		} while( (i-- > cache_spell_id) && !json_is_array(json_cmt) );

		if(json_is_array(json_cmt)) {
			*str = json_array_get_string_safe(json_cmt, line_num);
			return breakpoint_cave_exec_flag(bp_info);
		}
	}
	return 1;
}

int BP_spell_owner(x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	const char **spell_owner = (const char**)json_object_get_register(bp_info, regs, "spell_owner");
	// ----------

	// Other breakpoints
	// -----------------
	BP_spell_id(regs, bp_info);
	// -----------------

	if (spell_owner && cache_spell_id_real >= cache_spell_id) {
		const char *new_owner = NULL;
		int i = cache_spell_id_real;
		json_t *spellcomments = jsondata_game_get("spellcomments.js");

		// Count down from the real number to the given number
		// until we find something
		do {
			new_owner = json_string_value(json_object_get(json_object_numkey_get(spellcomments, i), "owner"));
		} while ((i-- > cache_spell_id) && i >= 0 && !new_owner);

		if (new_owner) {
			*spell_owner = new_owner;
			return breakpoint_cave_exec_flag(bp_info);
		}
	}
	return 1;
}

void spells_mod_init(void)
{
	jsondata_game_add("spells.js");
	jsondata_game_add("spellcomments.js");
}
