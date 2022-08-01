#include <iostream>
#include "grapheme.h"



char GSTemp1[GSTEMPLEN];
char GSTemp2[GSTEMPLEN];
char GSTemp3[GSTEMPLEN];
char GSTemp4[GSTEMPLEN];
char GSTemp5[GSTEMPLEN];
char GSTemp6[GSTEMPLEN];
char GSTemp7[GSTEMPLEN];
char GSTemp8[GSTEMPLEN];
char* GSTempIndex[NUMGSTEMPS] = {
	GSTemp1,GSTemp2,GSTemp3,GSTemp4,GSTemp5,GSTemp6,GSTemp7,GSTemp8
};
int CurGSTemp=0;

/*UTF8PROC_NULLTERM UTF8PROC_STABLE UTF8PROC_COMPOSE
UTF8PROC_DLLEXPORT utf8proc_ssize_t utf8proc_map(
  const utf8proc_uint8_t *str, utf8proc_ssize_t strlen, utf8proc_uint8_t **dstptr, utf8proc_option_t options
);

UTF8PROC_DLLEXPORT utf8proc_ssize_t utf8proc_iterate(const utf8proc_uint8_t *str, utf8proc_ssize_t strlen, utf8proc_int32_t *codepoint_ref);


*/

//GraphemeString_letter GraphemeString_letter::Null((uint8_t*)"",Singleton);



//returns number of bytes used
int codepoint_to_utf8(const int32_t codepoint, uint8_t* s) 
{
	if (codepoint < 128) {
		if (s!=nullptr) s[0] = codepoint;
		return 1;
	}
	else if ((uint32_t)codepoint < 0x800) {
		if (s != nullptr) {
			s[0] = (0xc0 | (codepoint >> 6));
			s[1] = (0x80 | (codepoint & 0x3f));
		}
		return 2;
	}
	else if ((uint32_t)codepoint < 0x10000) {
		if (s != nullptr) {
			s[0] = (0xe0 | (codepoint >> 12));
			s[1] = (0x80 | ((codepoint >> 6) & 0x3f));
			s[2] = (0x80 | (codepoint & 0x3f));
		}
		return 3;
	}
	else if ((uint32_t)codepoint < 0x200000) {
		if (s != nullptr) {
			s[0] = (0xf0 | (codepoint >> 18));
			s[1] = (0x80 | ((codepoint >> 12) & 0x3f));
			s[2] = (0x80 | ((codepoint >> 6) & 0x3f));
			s[3] = (0x80 | (codepoint & 0x3f));
		}
		return 4;
	}
	else return 0;
}
int fill_utf8_from_codepoints(const int32_t* source, uint8_t* dest)
{
	int sum = 0;
	while (*source != 0) {
		int t = codepoint_to_utf8(*source++, dest);
		if (t == 0) return -1;
		if (dest != nullptr)dest += t;
		sum += t;
	}
	if (dest != nullptr)*dest = 0;
	
	return sum;
}

LPSTR UnicodeToUTF8(LPCTSTR s)
{
	int bsize = WideCharToMultiByte(
		CP_UTF8,
		0,
		s,
		-1,
		NULL,
		0,
		NULL,
		NULL
	);
	LPSTR mbuffer = new CHAR[bsize + 1];

	WideCharToMultiByte(
		CP_UTF8,
		0,
		s,
		-1,
		mbuffer,
		bsize + 1,
		NULL,
		NULL
	);
	return mbuffer;
}

uint8_t GraphemeString::nullbyte = 0;
int32_t GraphemeString::nullcodepoint = 0;


void GraphemeString_letter::build(const std::vector<GraphemeString>& o)
{


	int utf8_size = 0;

	for (auto g : o) {
		utf8_size += g.byte_length();
	}

	uint8_t* store_normalized = (uint8_t*)malloc(utf8_size+1);
	std::unique_ptr<uint8_t, MyFreeDeleter> buf(store_normalized);

	int offset = 0;
	for (auto g : o) {
		g.fill_utf8(store_normalized + offset);
		offset += g.byte_length();
	}
	load(store_normalized);
}
void GraphemeString_letter::load(const GraphemeString& o1, const GraphemeString& o2)
{
	uint8_t* store_normalized = (uint8_t*)malloc(o1.byte_length() + o2.byte_length() + 1);
	std::unique_ptr<uint8_t, MyFreeDeleter> holder(store_normalized);
	o1.fill_utf8(store_normalized,false);
	o2.fill_utf8(store_normalized + o1.byte_length());
	load(store_normalized);
}


void GraphemeString_letter::load(const GraphemeString& o)
{
	uint8_t* store_normalized = (uint8_t*)malloc(o.byte_length() + 1);
	std::unique_ptr<uint8_t, MyFreeDeleter> holder(store_normalized);
	o.fill_utf8(store_normalized);
	load(store_normalized);
}

void GraphemeString_letter::load(const uint8_t* source)
{
	uint8_t* store_normalized;

	int utf8_size = utf8proc_map(source, 0, &store_normalized, (utf8proc_option_t)(UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_COMPOSE) //| UTF8PROC_NLF2LF
	);
	if (utf8_size < 0) {
		switch (utf8_size) {
		case UTF8PROC_ERROR_NOMEM:
			throw std::runtime_error("out of memory in GraphemeString load");
		case UTF8PROC_ERROR_OVERFLOW:
			throw std::runtime_error("string too long to process in GraphemeString load");
		case UTF8PROC_ERROR_INVALIDUTF8:
			throw std::domain_error("invalid unicode in GraphemeString load");
		case UTF8PROC_ERROR_NOTASSIGNED:
			throw std::domain_error("unassigned codepoint in GraphemeString load");
		case UTF8PROC_ERROR_INVALIDOPTS:
			throw std::logic_error("invalide options in GraphemeString load");

		default:
			throw std::runtime_error("unknown runtime error in GraphemeString load");
		}
	}
	utf8_buffer_size = utf8_size;

	utf8_buffer = std::move(std::unique_ptr<uint8_t, MyFreeDeleter>(store_normalized));
	
	codepoint_buffer.resize(1, 0);
	codepoint_to_utf8_index.resize(1, 0);
	grapheme_to_codepoint_index.resize(1, 0);
	int grapheme_state = UTF8PROC_BOUNDCLASS_START;

	int32_t prev_codepoint;

	for (int codepoint_index = 0;; ++codepoint_index) {

		codepoint_to_utf8_index.push_back(codepoint_to_utf8_index.back()
			+ utf8proc_iterate(store_normalized + codepoint_to_utf8_index.back(),
				-1,
				&codepoint_buffer.back()));
		if (codepoint_buffer.back() == -1 || codepoint_to_utf8_index.back()> utf8_buffer_size+1) {
			codepoint_buffer.pop_back();
			codepoint_to_utf8_index.pop_back();
			grapheme_to_codepoint_index.push_back(codepoint_index);
			break;
		}
		if (codepoint_index > 0 && utf8proc_grapheme_break_stateful(prev_codepoint, codepoint_buffer.back(), &grapheme_state)) {
			grapheme_to_codepoint_index.push_back(codepoint_index);
		}
		prev_codepoint = codepoint_buffer.back();
		codepoint_buffer.push_back(0);
	}
	log_size(sizeof(this) + 4 * (codepoint_buffer.size() + codepoint_to_utf8_index.size() + grapheme_to_codepoint_index.size()) + utf8_buffer_size);
}
