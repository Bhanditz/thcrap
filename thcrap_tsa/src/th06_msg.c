/**
  * Touhou Community Reliant Automatic Patcher
  * Team Shanghai Alice support plugin
  *
  * ----
  *
  * On-the-fly th06msg patcher (in-game dialog format *since* th06)
  *
  * Portions adapted from Touhou Toolkit
  * https://github.com/thpatch/thtk
  */

#include <thcrap.h>
#include "thcrap_tsa.h"
#include "layout.h"

#pragma pack(push, 1)
typedef struct {
	uint16_t time;
	uint8_t type;
	uint8_t length;
	uint8_t data[];
} th06_msg_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint16_t time;
	uint8_t type;
	uint8_t length;
	float x;
	float y;
} th128_bubble_pos_t;
#pragma pack(pop)

#pragma pack(push, 1)
typedef struct {
	uint16_t side;
	uint16_t linenum;
	uint8_t str[];
} hard_line_data_t;
#pragma pack(pop)

typedef uint32_t th14_bubble_shape_data_t;

// Supported opcode commands
typedef enum {
	OP_UNKNOWN = 0,
	OP_HARD_LINE,
	OP_AUTO_LINE,
	OP_AUTO_END,
	OP_DELETE,
	OP_SIDE_LEFT,
	OP_SIDE_RIGHT,
	OP_BUBBLE_POS,
	OP_BUBBLE_SHAPE,
} op_cmd_t;

typedef struct {
	uint8_t op;
	op_cmd_t cmd;
	const char *type;
} op_info_t;

/// Text encryption
/// ---------------
/**
  * Encryption function type.
  *
  * Parameters
  * ----------
  *	uint8_t* data
  *		Buffer to encrypt
  *
  *	size_t data_len
  *		Length of [data]
  *
  * Returns nothing.
  */
typedef void(*EncryptionFunc_t)(uint8_t *data, size_t data_len);

void simple_xor(uint8_t *data, size_t data_len, const uint8_t param)
{
	size_t i;
	for(i = 0; i < data_len; i++) {
		data[i] ^= param;
	}
}

void util_xor(
	uint8_t *data,
	size_t data_len,
	unsigned char key,
	unsigned char step,
	unsigned char step2
)
{
	size_t i;
	for(i = 0; i < data_len; i++) {
		const int ip = i - 1;
		data[i] ^= key + i * step + (ip * ip + ip) / 2 * step2;
	}
}

void msg_crypt_th08(uint8_t *data, size_t data_len)
{
	simple_xor(data, data_len, 0x77);
}

void msg_crypt_th09(uint8_t *data, size_t data_len)
{
	util_xor(data, data_len, 0x77, 0x7, 0x10);
}
/// --------------------

// Format information for a specific game
typedef struct {
	uint32_t entry_offset_mul;
	EncryptionFunc_t enc_func;
	op_info_t opcodes[];
} msg_format_t;

typedef enum {
	SIDE_LEFT = -1,
	SIDE_NONE = 0,
	SIDE_RIGHT = 1
} side_t;

typedef struct {
	const msg_format_t *format;

	// Can be any value, 0, negative, ... So, a pointer it is.
	json_t *font_dialog_id;

	// JSON objects in the diff file
	const json_t *diff_entry;
	const json_t *diff_lines;

	// Current input / output
	th06_msg_t* cmd_in;
	th06_msg_t* cmd_out;
	int entry, time, ind;
	side_t side;
	unsigned int line_widths[4];

	// Last state
	th128_bubble_pos_t *bubble_pos;
	const th06_msg_t *last_line_cmd;
	const op_info_t *last_line_op;

	// Indices
	size_t cur_line;
} patch_msg_state_t;

/**
  * Line replacement function type.
  *
  * Parameters
  * ----------
  *	th06_msg_t *cmd_out
  *		Target .msg command
  *
  *	patch_msg_state_t *state
  *		Patch state object
  *
  *  const char *rep
  *		Replacement string
  *
  * Returns nothing.
  */
typedef void (*ReplaceFunc_t)(th06_msg_t *cmd_out, patch_msg_state_t *state, const char *new_line);

const op_info_t* get_op_info(const msg_format_t* format, uint8_t op)
{
	const op_info_t *opcode = format->opcodes;
	if(!opcode) {
		return NULL;
	}
	while(opcode->op != 0) {
		if(opcode->op == op) {
			return opcode;
		}
		opcode++;
	}
	return NULL;
}

size_t th06_msg_full_len(const th06_msg_t* msg)
{
	if(!msg) {
		return 0;
	}
	return (sizeof(th06_msg_t) + msg->length);
}

th06_msg_t* th06_msg_advance(const th06_msg_t* msg)
{
	return (th06_msg_t*)((uint8_t*)msg + th06_msg_full_len(msg));
}

size_t get_len_at_last_codepoint(const char *str, const size_t limit)
{
	size_t ret = 0;
	while(str[ret]) {
		size_t old_ret = ret;
		ret++;
		// Every string that gets here is UTF-8 anyway.
		while((str[ret] & 0xc0) == 0x80) {
			ret++;
		}
		if(ret >= limit) {
			ret = old_ret;
			break;
		}
	}
	return ret + 1;
}

void replace_line(uint8_t *dst, const char *rep, const size_t len, patch_msg_state_t *state)
{
	memcpy(dst, rep, len);
	dst[len - 1] = '\0';

	// We might only cut the JSON line right now, but in view of everything
	// else we could possibly do, calculating the line length right here,
	// pre-encraption, seems to be the best choice after all.
	if(
		state->bubble_pos
		&& dst[0] != '|' // Don't take Ruby into account
		&& json_is_integer(state->font_dialog_id)
		&& state->cur_line < elementsof(state->line_widths)
	) {
		if(state->cur_line == 0) {
			log_printf("\xF0\x9F\x92\xAC\n"); // 💬
		}
		size_t font_id = (size_t)json_integer_value(state->font_dialog_id);
		size_t width = GetTextExtentForFontID(dst, font_id);
		state->line_widths[state->cur_line] = width;
	}

	if(state->format->enc_func) {
		state->format->enc_func(dst, len);
	}
}

void replace_auto_line(th06_msg_t *cmd_out, patch_msg_state_t *state, const char *rep)
{
	cmd_out->length = get_len_at_last_codepoint(rep, 255);
	replace_line(cmd_out->data, rep, cmd_out->length, state);
}

void replace_hard_line(th06_msg_t *cmd_out, patch_msg_state_t *state, const char *rep)
{
	hard_line_data_t* line = (hard_line_data_t*)cmd_out->data;
	size_t line_copy_len = get_len_at_last_codepoint(rep, 255 - 4);

	line->linenum = state->cur_line;
	cmd_out->length = line_copy_len + 4;
	replace_line(line->str, rep, line_copy_len, state);
}

void format_slot_key(char *key_str, int time, const char *msg_type, int time_ind)
{
	if(msg_type) {
		sprintf(key_str, "%d_%s_%d", time, msg_type, time_ind);
	} else {
		sprintf(key_str, "%d_%d", time, time_ind);
	}
}

int validate_line(const char *line)
{
	if(!line) {
		return 0;
	}
	// Validate that the string is valid TSA ruby syntax.
	// Important because the games themselves (surprise, suprise) don't verify
	// the return value of the strchr() call used to get the parameters.
	// Thus, they would simply crash if a | is not followed by two commas.
	if(line[0] == '|') {
		const char *p2 = strchr(line + 1, ',');
		return p2 ? strchr(p2 + 1, ',') != 0 : 0;
	}
	return 1;
}

// Returns 1 if the output buffer should advance, 0 if it shouldn't.
int process_line(th06_msg_t *cmd_out, patch_msg_state_t *state, ReplaceFunc_t rep_func)
{
	const op_info_t* cur_op = get_op_info(state->format, cmd_out->type);

	// If we don't have a diff_lines pointer, this is the first line of a new box.
	if(cur_op && !json_is_array(state->diff_lines)) {
		size_t key_str_len = strlen(cur_op->type) + 32;
		VLA(char, key_str, key_str_len);
		state->ind++;
		format_slot_key(key_str, state->time, cur_op->type, state->ind);
		state->diff_lines = json_object_get(state->diff_entry, key_str);
		if(json_is_object(state->diff_lines)) {
			state->diff_lines = json_object_get(state->diff_lines, "lines");
		}
		VLA_FREE(key_str);
	}

	if(json_is_array(state->diff_lines)) {
		const char *json_line = json_array_get_string(state->diff_lines, state->cur_line);
		int ret = validate_line(json_line);
		if(ret) {
			rep_func(cmd_out, state, json_line);
			state->last_line_cmd = cmd_out;
			state->last_line_op = cur_op;
		}
		state->cur_line++;
		return ret;
	}
	// If this dialog box contains no lines to patch, take original line
	return 1;
}

void box_end(patch_msg_state_t *state)
{
	if(!state->last_line_op) {
		return;
	}

	// Do we have any extra lines in the patch file?
	while(state->cur_line < json_array_size(state->diff_lines)) {
		const char *json_line = json_array_get_string(state->diff_lines, state->cur_line);
		if(validate_line(json_line)) {
			th06_msg_t *new_line_cmd = th06_msg_advance(state->last_line_cmd);
			ptrdiff_t move_len;
			int hard_line;
			size_t extra_param_len;
			size_t line_offset;
			size_t line_len_trimmed;

			move_len = (uint8_t*)th06_msg_advance(state->cmd_out) - (uint8_t*)new_line_cmd;

			hard_line = (state->last_line_op->cmd == OP_HARD_LINE);
			extra_param_len = hard_line ? 4 : 0;
			line_offset = sizeof(th06_msg_t) + extra_param_len;
			line_len_trimmed = get_len_at_last_codepoint(json_line, 255 - extra_param_len);

			// Make room for the new line
			memmove(
				(uint8_t*)(new_line_cmd) + line_offset + line_len_trimmed,
				new_line_cmd,
				move_len
			);
			memcpy(new_line_cmd, state->last_line_cmd, line_offset);
			process_line(new_line_cmd, state, hard_line ? replace_hard_line : replace_auto_line);
			// Meh, pointer arithmetic.
			state->cmd_out = (th06_msg_t*)((uint8_t*)state->cmd_out + th06_msg_full_len(new_line_cmd));
		} else {
			state->cur_line++;
		}
	}

	if(state->bubble_pos) {
		// Conveniently assuming that the lines from the .msg file
		// don't need to be taken into account...
		unsigned int box_len = 0;
		size_t i;
		size_t largest_line;
		int do_shift = 0;
		float overhang;

		assert(state->side != SIDE_NONE);

		for(i = 0; i < elementsof(state->line_widths); i++) {
			if(state->line_widths[i] > box_len) {
				box_len = state->line_widths[i];
				largest_line = i;
			}
			state->line_widths[i] = 0;
		}
		// text.anm limit
		box_len = min(box_len, 512);

		// Padding. TODO: Derive from text.anm,
		// once we have an ANM spec patcher?
		if(game_id == TH13) {
			box_len += 13;
		} else if(game_id == TH128) {
			box_len += 24;
		} else {
			box_len += 19;
		};

		if(state->side == SIDE_LEFT) {
			overhang = (state->bubble_pos->x + box_len) - 640.0f;
			do_shift = overhang > 0.0f;
		} else if(state->side == SIDE_RIGHT) {
			overhang = (state->bubble_pos->x - box_len);
			do_shift = overhang < 0.0f;
		}
		if(do_shift) {
			state->bubble_pos->x -= overhang;

			const char *side_str = state->side == SIDE_LEFT ? "left" : "right";
			log_printf(
				"[MSG] Speech bubble is shifted %.0f pixels to the %s to fit these lines\n",
				(overhang < 0.0f ? -overhang : overhang) , side_str
			);
		}
		log_printf("\n"); // for better log readability
		state->bubble_pos = NULL;
	}
	state->cur_line = 0;
	state->diff_lines = NULL;
}

int op_auto_end(patch_msg_state_t* state)
{
	if(!state->last_line_op || state->last_line_op->cmd == OP_AUTO_LINE) {
		box_end(state);
	}
	return 1;
}

// Returns whether to advance the output buffer (1) or not (0)
int process_op(const op_info_t *cur_op, patch_msg_state_t* state)
{
	int ret = 0;
	hard_line_data_t* line;
	th14_bubble_shape_data_t *shape;
	uint16_t linenum;

	switch(cur_op->cmd) {
		case OP_DELETE:
			// Overwrite this command with the next one
			return 0;

		// Since these can be used to show a new box without a wait
		// command before, we have to do the end-of-box processing
		// *before* we change the side. See the TH16 Extra Stage
		// mid-boss dialog for a case where this matters.
		case OP_SIDE_LEFT:
			ret = op_auto_end(state);
			state->side = SIDE_LEFT;
			return ret;

		case OP_SIDE_RIGHT:
			ret = op_auto_end(state);
			state->side = SIDE_RIGHT;
			return ret;

		case OP_BUBBLE_SHAPE:
			shape = (th14_bubble_shape_data_t *)state->cmd_out->data;
			assert(state->cmd_out->length >= 4);
			// As of TH14, shape values from [0; 15] are valid, larger
			// ones crash the game. Not our problem though, who knows,
			// maybe they'll be added in a future game.

			state->side = (*shape & 1) ? SIDE_RIGHT : SIDE_LEFT;
			return op_auto_end(state);

		case OP_BUBBLE_POS:
			state->bubble_pos = (th128_bubble_pos_t*)state->cmd_out;
			assert(state->bubble_pos->length >= 8);
			return 1;

		case OP_AUTO_LINE:
			if( (state->last_line_op) && (state->last_line_op->cmd == OP_HARD_LINE) ) {
				box_end(state);
			}
			return process_line(state->cmd_out, state, replace_auto_line);

		case OP_HARD_LINE:
			line = (hard_line_data_t*)state->cmd_out->data;
			// needs to be saved locally because box_end trashes the pointer
			linenum = line->linenum;

			if(linenum == 0) {
				// Line 0 always terminates a previous box
				box_end(state);
				if(state->last_line_op) {
					// Reset index when last_line_op->type differs from cur_op->type.
					// Needed e.g. for th07 stage 3
					const char *last_op_type = state->last_line_op->type;
					if(
						(!last_op_type && (last_op_type != cur_op->type)) 	||
						(last_op_type && !cur_op->type) ||
						(last_op_type && cur_op->type && !strcmp(last_op_type, cur_op->type))
					) {
						state->ind = -1;
					}
				}
			}
			state->cur_line = linenum;
			return process_line(state->cmd_out, state, replace_hard_line);

		case OP_AUTO_END:
			return op_auto_end(state);
	}
	return 1;
}

json_t* font_dialog_id(void)
{
	// Yeah, I know, kinda horrible.
	json_t *breakpoints = json_object_get(runconfig_get(), "breakpoints");
	json_t *ruby_offset_info = json_object_get(breakpoints, "ruby_offset");
	return json_object_get(ruby_offset_info, "font_dialog");
}

int patch_msg(uint8_t *file_inout, size_t size_out, size_t size_in, json_t *patch, const msg_format_t *format)
{
	assert(format);
	assert(format->entry_offset_mul);

	uint32_t* msg_out = (uint32_t*)file_inout;
	uint32_t* msg_in;
	int entry_new = 1;

	// Header data
	size_t entry_count;
	uint32_t *entry_offsets_out;
	uint32_t *entry_offsets_in;
	uint32_t entry_offset_size;

	patch_msg_state_t state = {format, font_dialog_id()};

	if(!msg_out || !patch) {
		return 0;
	}

	// Make a copy of the input buffer
	msg_in = (uint32_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size_in);
	memcpy(msg_in, msg_out, size_in);

	/*
	 * Unnecessary, because we don't read from the output buffer and
	 * the game stops reading at the last entry's 0 opcode anyway.
	 */
	// ZeroMemory(msg_out, size_out);

#ifdef _DEBUG
	{
		FILE* out = fopen("in.msg", "wb");
		if(out) {
			fwrite(msg_in, size_in, 1, out);
			fclose(out);
		}
	}
#endif

	// Read .msg header
	entry_count = *msg_in;
	entry_offsets_in = msg_in + 1;

	entry_offset_size = entry_count * format->entry_offset_mul;

	state.cmd_in = (th06_msg_t*)(msg_in + 1 + entry_offset_size);

	*msg_out = entry_count;
	entry_offsets_out = msg_out + 1;

	// Include whatever junk there might be in the header
	memcpy(entry_offsets_out, entry_offsets_in, entry_offset_size);

	state.cmd_out = (th06_msg_t*)(msg_out + 1 + entry_offset_size);

	for(;;) {
		const ptrdiff_t offset_in  = (uint8_t*)state.cmd_in - (uint8_t*)msg_in;
		const ptrdiff_t offset_out = (uint8_t*)state.cmd_out - (uint8_t*)msg_out;
		int advance_out = 1;
		const op_info_t* cur_op;

		if((uint32_t)offset_in >= size_in
			/*|| (entry_new && state.entry == entry_count - 1)*/
			// (sounds like a good idea, but breaks th09 Marisa)
		) {
			break;
		}
		if(state.cmd_in->time == 0 && state.cmd_in->type == 0) {
			// Last command of the entry
			// Next command will start a new one
			entry_new = 1;

			// Close any open text boxes.
			// Very important - if the last box of an entry should receive a new line,
			// it would otherwise be created in the context of the next entry,
			// messing up the entry offsets in the process.
			// Fixes bug #3 (https://bitbucket.org/nmlgc/thpatch-bugs/issue/3)
			box_end(&state);
		}

		if(entry_new && state.cmd_in->type != 0) {
			// First command of a new entry.
			// Get entry object from the diff file,
			// and assign the correct offset to the output buffer.
			size_t i;
			for(i = 0; i < entry_count; ++i) {
				if(offset_in == entry_offsets_in[i * format->entry_offset_mul]) {
					state.entry = i;
					state.diff_entry = json_object_numkey_get(patch, state.entry);
					entry_offsets_out[i * format->entry_offset_mul] = offset_out;
					break;
				}
			}
			if(game_id >= TH128) {
				log_printf("---- Entry #%d ----\n", state.entry);
			}
			entry_new = 0;
			state.time = -1;
		}

		if(state.cmd_in->time != state.time) {
			// New timecode. Reset index
			state.time = state.cmd_in->time;
			state.ind = -1;
		}

		// Firstly, copy this command to the output buffer
		// If there's anything to change, we do so later
		memcpy(state.cmd_out, state.cmd_in, sizeof(th06_msg_t) + state.cmd_in->length);

		// Look up what to do with the opcode...
		cur_op = get_op_info(state.format, state.cmd_in->type);

		if(cur_op && json_is_object(state.diff_entry)) {
			advance_out = process_op(cur_op, &state);
		}

		state.cmd_in = th06_msg_advance(state.cmd_in);
		if(advance_out) {
			state.cmd_out = th06_msg_advance(state.cmd_out);
		}
	}

#ifdef _DEBUG
	{
		FILE* out = fopen("out.msg", "wb");
		if(out) {
			fwrite(msg_out, (uint8_t*)state.cmd_out - (uint8_t*)msg_out, 1, out);
			fclose(out);
		}
	}
#endif
	HeapFree(GetProcessHeap(), 0, msg_in);
	return 1;
}

/// Game formats
/// ------------
// In-game dialog
const msg_format_t MSG_TH06 = {
	.entry_offset_mul = 1,
	.opcodes = {
		{ 3, OP_HARD_LINE },
		{ 8, OP_HARD_LINE, "h1" },
		{ 0 }
	}
};

const msg_format_t MSG_TH08 = {
	.entry_offset_mul = 1,
	.enc_func = msg_crypt_th08,
	.opcodes = {
		{  3, OP_HARD_LINE },
		{  4, OP_AUTO_END },
		{ 15, OP_AUTO_END },
		{ 16, OP_AUTO_LINE },
		{ 19, OP_AUTO_LINE },
		{ 20, OP_AUTO_LINE },
		{ 0 }
	}
};

const msg_format_t MSG_TH09 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{  4, OP_AUTO_END },
		{ 15, OP_AUTO_END },
		{ 16, OP_AUTO_LINE },
		{ 0 }
	}
};

const msg_format_t MSG_TH10 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{  7, OP_AUTO_END },
		{  8, OP_AUTO_END },
		{ 10, OP_AUTO_END },
		{ 16, OP_AUTO_LINE },
		{ 0 }
	}
};

const msg_format_t MSG_TH11 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{  7, OP_AUTO_END },
		{  8, OP_AUTO_END },
		{  9, OP_AUTO_END },
		{ 11, OP_AUTO_END },
		{ 17, OP_AUTO_LINE },
		{ 25, OP_DELETE },
		{ 0 }
	}
};

const msg_format_t MSG_TH128 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{  7, OP_SIDE_LEFT },
		{  8, OP_SIDE_RIGHT },

		// Only used for the Parallel Ending explanation
		// at the end of TH13's Extra Stage.
		{  9, OP_SIDE_LEFT },

		{ 11, OP_AUTO_END },
		{ 17, OP_AUTO_LINE },
		{ 25, OP_DELETE },
		{ 28, OP_BUBBLE_POS },
		{ 0 }
	}
};

const msg_format_t MSG_TH14 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{ 7, OP_SIDE_LEFT },
		{ 8, OP_SIDE_RIGHT },
		{ 9, OP_SIDE_LEFT }, // unused
		{ 11, OP_AUTO_END },
		{ 17, OP_AUTO_LINE },
		{ 25, OP_DELETE },
		{ 28, OP_BUBBLE_POS },
		{ 32, OP_BUBBLE_SHAPE },
		{ 0 }
}
};

// Endings (TH10 and later)
const msg_format_t END_TH10 = {
	.entry_offset_mul = 2,
	.enc_func = msg_crypt_th09,
	.opcodes = {
		{ 3, OP_AUTO_LINE },
		{ 5, OP_AUTO_END },
		{ 6, OP_AUTO_END },
		{ 9, OP_AUTO_END },
		{ 0 }
	}
};

const msg_format_t* msg_format_for(tsa_game_t game)
{
	if(game >= TH14) {
		return &MSG_TH14;
	} else if(game >= TH128) {
		return &MSG_TH128;
	} else if(game >= TH11) {
		return &MSG_TH11;
	} else if(game >= TH10) {
		return &MSG_TH10;
	} else if(game >= TH09) {
		return &MSG_TH09;
	} else if(game >= TH08) {
		return &MSG_TH08;
	}
	return &MSG_TH06;
}
/// ------------

int patch_msg_dlg(uint8_t *file_inout, size_t size_out, size_t size_in, const char *fn, json_t *patch)
{
	const msg_format_t *format = msg_format_for(game_id);
	return patch_msg(file_inout, size_out, size_in, patch, format);
}

int patch_msg_end(uint8_t *file_inout, size_t size_out, size_t size_in, const char *fn, json_t *patch)
{
	return patch_msg(file_inout, size_out, size_in, patch, &END_TH10);
}
