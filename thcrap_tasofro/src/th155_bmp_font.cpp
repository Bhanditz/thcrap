/**
  * Touhou Community Reliant Automatic Patcher
  * Tasogare Frontier support plugin
  *
  * ----
  *
  * Image patching for nsml engine.
  */

#include <thcrap.h>
#include "./png.h"
#include "thcrap_tasofro.h"
#include "th155_bmp_font.h"
#include "bmpfont_create.h"
#include <string>

size_t get_bmp_font_size(const char *fn, json_t *patch, size_t patch_size)
{
	if (patch) {
		// TODO: generate and cache the file here, return the true size.
		return 64 * 1024 * 1024;
	}

	size_t size = 0;

	VLA(char, fn_png, strlen(fn) + 5);
	strcpy(fn_png, fn);
	strcat(fn_png, ".png");

	uint32_t width, height;
	bool IHDR_read = png_image_get_IHDR(fn_png, &width, &height, nullptr);
	if (IHDR_read) {
		size += sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + width * height;
	}

	VLA(char, fn_bin, strlen(fn) + 5);
	strcpy(fn_bin, fn);
	strcat(fn_bin, ".bin");

	size_t bin_size;
	void *bin_data = stack_game_file_resolve(fn_bin, &bin_size);
	if (bin_data) {
		free(bin_data);
		size += bin_size;
	}

	VLA_FREE(fn_png);
	VLA_FREE(fn_bin);

	return size;
}

void add_json_file(bool *chars_list, int& chars_list_count, json_t *file)
{
	if (json_is_object(file)) {
		const char *key;
		json_t *it;
		json_object_foreach(file, key, it) {
			json_incref(it);
			add_json_file(chars_list, chars_list_count, it);
		}
	}
	else if (json_is_array(file)) {
		size_t i;
		json_t *it;
		json_array_foreach(file, i, it) {
			json_incref(it);
			add_json_file(chars_list, chars_list_count, it);
		}
	}
	else if (json_is_string(file)) {
		const char *str = json_string_value(file);
		WCHAR_T_DEC(str);
		WCHAR_T_CONV(str);
		for (int i = 0; str_w[i]; i++) {
			if (chars_list[str_w[i]] == false) {
				chars_list[str_w[i]] = true;
				chars_list_count++;
			}
		}
		WCHAR_T_FREE(str);
	}
	json_decref(file);
}

static void add_files_in_directory(bool *chars_list, int& chars_list_count, std::string basedir, bool recurse)
{
	std::string pattern = basedir + "\\*";
	WIN32_FIND_DATAA ffd;
	HANDLE hFind = FindFirstFile(pattern.c_str(), &ffd);
	if (hFind == INVALID_HANDLE_VALUE) {
		return;
	}

	do
	{
		if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) {
			// Do nothing
		}
		else if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
			if (recurse) {
				std::string dirname = basedir + "\\" + ffd.cFileName;
				add_files_in_directory(chars_list, chars_list_count, dirname, recurse);
			}
		}
		else if (strcmp(PathFindExtensionA(ffd.cFileName), ".js") == 0 ||
				 strcmp(PathFindExtensionA(ffd.cFileName), ".jdiff") == 0) {
			std::string filename = basedir + "\\" + ffd.cFileName;
			log_printf(" + %s\n", filename.c_str());
			add_json_file(chars_list, chars_list_count, json_load_file(filename.c_str(), 0, nullptr));
		}
	} while (FindNextFile(hFind, &ffd));

	FindClose(hFind);
}

// Returns the number of elements set to true in the list
int fill_chars_list(bool *chars_list, void *file_inout, size_t size_in, json_t *files)
{
	// First, add the characters of the original font - we will need them if we display unpatched text.
	BITMAPFILEHEADER *bpFile = (BITMAPFILEHEADER*)file_inout;
	BYTE *metadata = (BYTE*)file_inout + bpFile->bfSize;
	int nb_chars = *(uint16_t*)(metadata + 2);
	char *chars = (char*)(metadata + 4);
	int chars_list_count = 0;

	for (int i = 0; i < nb_chars; i++) {
		char cstr[2];
		WCHAR wstr[3];
		if (chars[i * 2 + 1] != 0)
		{
			cstr[0] = chars[i * 2 + 1];
			cstr[1] = chars[i * 2];
		}
		else
		{
			cstr[0] = chars[i * 2];
			cstr[1] = 0;
		}
		MultiByteToWideChar(932, 0, cstr, 2, wstr, 3);

		if (chars_list[wstr[0]] == false) {
			chars_list[wstr[0]] = true;
			chars_list_count++;
		}
	}

	// Then, add all the characters that we could possibly want to display.
	size_t i;
	json_t *it;
	json_flex_array_foreach(files, i, it) {
		const char *fn = json_string_value(it);
		if (!fn) {
			// Do nothing
		}
		else if (strcmp(fn, "*") == 0) {
			log_print("(Font) Searching in every js file for characters...\n");
			json_t *patches = json_object_get(runconfig_get(), "patches");
			json_incref(patches);
			json_t *subtitles = json_object_get(runconfig_get(), "subtitles");
			if (subtitles) {
				json_t *all_patches = json_copy(patches);
				json_array_extend(all_patches, subtitles);
				json_decref(patches);
				patches = all_patches;
			}

			size_t i;
			json_t *patch_info;
			json_array_foreach(patches, i, patch_info) {
				const json_t *archive = json_object_get(patch_info, "archive");
				const json_t *game = json_object_get(runconfig_get(), "game");
				if (json_is_string(archive)) {
					std::string archive_path = json_string_value(archive);
					add_files_in_directory(chars_list, chars_list_count, archive_path, false);
					if (json_is_string(game)) {
						add_files_in_directory(chars_list, chars_list_count, archive_path + "\\" + json_string_value(game), true);
					}
				}
			}

			json_decref(patches);
		}
		/* else if (strchr(fn, '*') != nullptr) {
			// TODO: pattern matching
		} */
		else {
			add_json_file(chars_list, chars_list_count, stack_json_resolve(fn, nullptr));
		}
	}

	return chars_list_count;
}

void bmpfont_update_cache(std::string fn, bool *chars_list, int chars_count, BYTE *buffer, size_t buffer_size, json_t *patch)
{
	json_t *cache_patch = json_object();
	json_object_set(cache_patch, "patch", patch);
	json_object_set_new(cache_patch, "chars_count", json_integer(chars_count));
	json_t *chars_list_json = json_array();
	for (WCHAR i = 0; i < 65535; i++) {
		json_array_append_new(chars_list_json, json_boolean(chars_list[i]));
	}
	json_object_set(cache_patch, "chars_list", chars_list_json);
	chars_list_json = json_decref_safe(chars_list_json);

	std::string full_path;
	{
		std::string fn_jdiff = fn + ".jdiff";
		json_t *chain = resolve_chain_game(fn_jdiff.c_str());
		char *c_full_path = stack_fn_resolve_chain(chain);
		json_decref(chain);
		full_path.assign(c_full_path, strlen(c_full_path) - strlen(".jdiff")); // Remove ".jdiff"
		free(c_full_path);
	}

	full_path += ".cache";
	file_write(full_path.c_str(), buffer, buffer_size);

	full_path += ".jdiff";
	json_dump_file(cache_patch, full_path.c_str(), JSON_INDENT(2));

	json_decref(cache_patch);
}

BYTE *read_bmpfont_from_cache(std::string fn, bool *chars_list, int chars_count, json_t *patch, size_t *file_size)
{
	std::string cache_patch_fn = fn + ".cache.jdiff";
	json_t *cache_patch = stack_game_json_resolve(cache_patch_fn.c_str(), nullptr);
	if (!cache_patch) {
		return nullptr;
	}

	if (json_equal(patch, json_object_get(cache_patch, "patch")) == 0) {
		json_decref(cache_patch);
		return nullptr;
	}

	if (chars_count != json_integer_value(json_object_get(cache_patch, "chars_count"))) {
		json_decref(cache_patch);
		return nullptr;
	}
	json_t *chars_list_cache = json_object_get(cache_patch, "chars_list");
	for (WCHAR i = 0; i < 65535; i++) {
		if (chars_list[i] != json_boolean_value(json_array_get(chars_list_cache, i))) {
			json_decref(chars_list_cache);
			json_decref(cache_patch);
			return nullptr;
		}
	}
	json_decref(chars_list_cache);
	json_decref(cache_patch);

	std::string cache_fn = fn + ".cache";
	return (BYTE*)stack_game_file_resolve(cache_fn.c_str(), file_size);
}

int patch_bmp_font(void *file_inout, size_t size_out, size_t size_in, const char *fn, json_t *patch)
{
	if (patch) {
		// List all the characters to include in out font
		bool *chars_list = new bool[65536];
		for (int i = 0; i < 65536; i++) {
			chars_list[i] = false;
		}
		int chars_count = 0;
		chars_count = fill_chars_list(chars_list, file_inout, size_in, json_object_get(patch, "chars_source"));

		// Create the bitmap font
		size_t output_size;
		BYTE *buffer = read_bmpfont_from_cache(fn, chars_list, chars_count, patch, &output_size);
		if (buffer == nullptr) {
			buffer = generate_bitmap_font(chars_list, chars_count, patch, &output_size);
			if (buffer) {
				bmpfont_update_cache(fn, chars_list, chars_count, buffer, output_size, patch);
			}
		}
		delete[] chars_list;
		if (!buffer) {
			log_print("Bitmap font creation failed\n");
			return -1;
		}
		if (output_size > size_out) {
			log_print("Bitmap font too big\n");
			delete[] buffer;
			return -1;
		}

		memcpy(file_inout, buffer, output_size);
		delete[] buffer;
		return 1;
	}

	BITMAPFILEHEADER *bpFile = (BITMAPFILEHEADER*)file_inout;
	BITMAPINFOHEADER *bpInfo = (BITMAPINFOHEADER*)(bpFile + 1);
	BYTE *bpData = (BYTE*)(bpInfo + 1);

	VLA(char, fn_png, strlen(fn) + 5);
	strcpy(fn_png, fn);
	strcat(fn_png, ".png");

	uint32_t width, height;
	uint8_t bpp;
	BYTE** row_pointers = png_image_read(fn_png, &width, &height, &bpp, false);
	VLA_FREE(fn_png);

	if (row_pointers) {
		if (!row_pointers || size_out < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + width * height) {
			log_print("Destination buffer too small!\n");
			free(row_pointers);
			return -1;
		}

		bpFile->bfType = 0x4D42;
		bpFile->bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + width * height;
		bpFile->bfReserved1 = 0;
		bpFile->bfReserved2 = 0;
		bpFile->bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

		bpInfo->biSize = sizeof(BITMAPINFOHEADER);
		bpInfo->biWidth = width / 4;
		bpInfo->biHeight = height;
		bpInfo->biPlanes = 1;
		bpInfo->biBitCount = 32;
		bpInfo->biCompression = BI_RGB;
		bpInfo->biSizeImage = 0;
		bpInfo->biXPelsPerMeter = 0;
		bpInfo->biYPelsPerMeter = 0;
		bpInfo->biClrUsed = 0;
		bpInfo->biClrImportant = 0;

		for (unsigned int h = 0; h < height; h++) {
			for (unsigned int w = 0; w < width; w++) {
				bpData[h * width + w] = row_pointers[height - h - 1][w];
			}
		}
		free(row_pointers);
	}

	VLA(char, fn_bin, strlen(fn) + 5);
	strcpy(fn_bin, fn);
	strcat(fn_bin, ".bin");

	size_t bin_size;
	void *bin_data = stack_game_file_resolve(fn_bin, &bin_size);
	if (bin_data) {
		if (size_out < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + bpInfo->biWidth * 4 * bpInfo->biHeight + bin_size) {
			log_print("Destination buffer too small!\n");
			free(bin_data);
			return -1;
		}

		void *metadata = (BYTE*)file_inout + bpFile->bfSize;
		memcpy(metadata, bin_data, bin_size);

		free(bin_data);
	}
	return 1;
}

extern "C" int BP_c_bmpfont_fix_parameters(DWORD xmm0, DWORD xmm1, DWORD xmm2, x86_reg_t *regs, json_t *bp_info)
{
	// Parameters
	// ----------
	const char *string = (const char*)json_object_get_immediate(bp_info, regs, "string");
	size_t string_location = json_object_get_immediate(bp_info, regs, "string_location");
	int *offset = (int*)json_object_get_pointer(bp_info, regs, "offset");
	size_t *c1 = json_object_get_pointer(bp_info, regs, "c1");
	size_t *c2 = json_object_get_pointer(bp_info, regs, "c2");
	// ----------

	if (string_location >= 0x10) {
		string = *(const char**)string;
	}
	string += *offset;

	wchar_t wc[2];
	size_t codepoint_len = CharNextU(string) - string;
	MultiByteToWideCharU(0, 0, string, codepoint_len, wc, 2);
	*c1 = wc[0] >> 8;
	*c2 = wc[0] & 0xFF;

	*offset += codepoint_len - 1;

	return 1;
}
