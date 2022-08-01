#ifndef GRAPHEME_STRING_H
#define GRAPHEME_STRING_H

#ifndef VC_EXTRALEAN
#define VC_EXTRALEAN            // Exclude rarely-used stuff from Windows headers
#endif
#include <windows.h>
#include <vector>
#include "CollectableHash.h"

//#include <boost/intrusive_ptr.hpp>
//#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <iostream>

#define UTF8PROC_STATIC
//note it's getting stddef and others from utf8proc.h
#include "utf8proc/utf8proc.h"
#include "spooky.h"

#define GSTEMPLEN 200
extern char GSTemp1[GSTEMPLEN];
extern char GSTemp2[GSTEMPLEN];
extern char GSTemp3[GSTEMPLEN];
extern char GSTemp4[GSTEMPLEN];
extern char GSTemp5[GSTEMPLEN];
extern char GSTemp6[GSTEMPLEN];
extern char GSTemp7[GSTEMPLEN];
extern char GSTemp8[GSTEMPLEN];
#define NUMGSTEMPS 8 
extern char* GSTempIndex[8];
extern int CurGSTemp;
struct MyFreeDeleter { // deleter
	void operator() (uint8_t* p) {
		free(p);
	}
};
int fill_utf8_from_codepoints(const int32_t* source, uint8_t* dest=nullptr);
int codepoint_to_utf8(const int32_t codepoint, uint8_t* s = nullptr);

class GraphemeString;

enum SingletonEnum {
	Singleton
};

class GraphemeString_letter : public Collectable
{

public:
	int utf8_buffer_size;
	std::unique_ptr<uint8_t, MyFreeDeleter> utf8_buffer;//allocated by utf8proc library
	std::vector<int32_t> codepoint_buffer;
	std::vector<int> codepoint_to_utf8_index;
	std::vector<int> grapheme_to_codepoint_index;

//static GraphemeString_letter Null;

	void load(const GraphemeString&);
	void load(const GraphemeString&, const GraphemeString&);
	void load(const uint8_t* source);
	void build(const std::vector<GraphemeString>& o);

	GraphemeString_letter(const std::vector<GraphemeString>& o) { build(o); };

	GraphemeString_letter(const uint8_t* source) { load(source); };
	//GraphemeString_letter(const uint8_t* source, SingletonEnum) { intrusive_ptr_add_ref(this); load(source); };
	GraphemeString_letter(const int32_t* source)
	{
		int len = fill_utf8_from_codepoints(source);
		if (len > 32) {
			std::unique_ptr<uint8_t> buf2(new uint8_t[len + 1]);
			fill_utf8_from_codepoints(source, &*buf2);

			load(&*buf2);
		}
		else {
			uint8_t buf[33];
			fill_utf8_from_codepoints(source, buf);

			load(buf);
		}

	};

	InstancePtrBase* index_into_instance_vars(int num) { return nullptr; }
	int total_instance_vars() const { return 0; }


	GraphemeString_letter(const GraphemeString& source) { load(source); };
	GraphemeString_letter(const GraphemeString& src1, const GraphemeString& src2) { load(src1, src2); };
};


class GraphemeIterator;
class RGraphemeIterator;
class GraphemeStringBuilder;
LPSTR UnicodeToUTF8(LPCTSTR s);
class GraphemeString {
	RootPtr< const GraphemeString_letter> source;
	int g_start;
	int g_end;
	friend class GraphemeIterator;
	friend struct GraphemeString_letter;
	uint64_t hash_value1;
	uint64_t hash_value2;

	void fill_hash() {
		hash_value1 = 0x1E09C1AE8B8BD53EL;
		hash_value2 = 0x98DDC5A77F2B363AL;
		spooky_hash128(&*source->utf8_buffer + byte_start_slice_index(), byte_length(), &hash_value1, &hash_value2);
	}
	GraphemeString(const RootPtr<const GraphemeString_letter> &s, int st, int ed) :source(s), g_start(st), g_end(ed) { 
		fill_hash(); 
	}


	int grapheme_start_slice_index() const { return g_start; }
	int grapheme_end_slice_index() const { return g_end; }

	int codepoint_start_slice_index() const { return source->grapheme_to_codepoint_index[g_start]; }
	int codepoint_end_slice_index() const { return source->grapheme_to_codepoint_index[g_end-1]; }

	int byte_start_slice_index() const { return source->codepoint_to_utf8_index[source->grapheme_to_codepoint_index[g_start]]; }
	int byte_end_slice_index() const { return source->codepoint_to_utf8_index[source->grapheme_to_codepoint_index[g_end-1]]; }

public:

	uint64_t hash1() const { return hash_value1; }
	uint64_t hash2() const { return hash_value2; }

	void fill_utf8(uint8_t* dest, bool null_terminate = true) const {
		memcpy(dest, &*source->utf8_buffer + byte_start_slice_index(), byte_length());
		if (null_terminate)dest[byte_length()] = 0;
	}
	void fill_utf8n(uint8_t* dest, int maxlen , bool null_terminate = true) const {
		int m = byte_length();
		if (m >= maxlen) m = maxlen-1;
		memcpy(dest, &*source->utf8_buffer + byte_start_slice_index(), maxlen);
		if (null_terminate)dest[m] = 0;
	}

	const char *str()
	{
		int i = ++CurGSTemp&(NUMGSTEMPS-1);
		fill_utf8n((uint8_t *)&GSTempIndex[i][0], GSTEMPLEN);
		return GSTempIndex[i];
	}

	void fill_codepoints(int32_t* dest, bool null_terminate = true) const {
		memcpy(dest, &source->codepoint_buffer[0] + codepoint_start_slice_index(), codepoint_length() * sizeof(int32_t));
		if (null_terminate)dest[codepoint_length()] = 0;
	}
	int wchar_t_length() const {
		return MultiByteToWideChar(
			CP_UTF8,
			0,
			(LPCCH)(&*source->utf8_buffer + byte_start_slice_index()),
			byte_length(),
			NULL,
			0
		);
	}
	void fill_wchar_t(wchar_t* dest, bool null_terminate = true) const {
		int len = wchar_t_length();
		MultiByteToWideChar(
			CP_UTF8,
			0,
			(LPCCH)(&*source->utf8_buffer + byte_start_slice_index()),
			byte_length(),
			dest,
			len
		);
		if (null_terminate) dest[len] = 0;
	}

	int grapheme_length() const { return g_end - g_start; }
	int length() const { return g_end - g_start; }
	int size() const { return g_end - g_start; }

	int codepoint_length() const { return g_start == g_end ? 0 :(codepoint_end_slice_index() - codepoint_start_slice_index()); }
	int byte_length() const { return g_start == g_end ? 0 : (byte_end_slice_index() - byte_start_slice_index()); }

	GraphemeString operator +(const GraphemeString& o) const
	{
		return GraphemeString(RootPtr<const GraphemeString_letter>(new GraphemeString_letter(*this, o)), 0, size() + o.size() - 1); // {} {}{}?
	}

	static uint8_t nullbyte;
	static int32_t nullcodepoint;

	const uint8_t& byte_at(int i) const {
		if (i >= byte_length() || i < 0) return nullbyte;
		return (&*source->utf8_buffer)[i + byte_start_slice_index()];
	}
	const int32_t& codepoint_at(int i) const
	{
		if (i >= codepoint_length() || i < 0) return nullcodepoint;
		return source->codepoint_buffer[i + codepoint_start_slice_index()];
	}

	const int32_t& grapheme_num_codepoints(int i) const
	{
		if (i >= grapheme_length() || i < 0) return 0;
		return source->grapheme_to_codepoint_index[1 + i + grapheme_start_slice_index()]-source->grapheme_to_codepoint_index[i + grapheme_start_slice_index()];
	}

	const int32_t& grapheme_at(int i, int offset=0) const
	{
		if (i >= grapheme_length() || i < 0) return nullcodepoint;
		return source->codepoint_buffer[offset + source->grapheme_to_codepoint_index[i + grapheme_start_slice_index()]];
	}

	GraphemeString slice(int from, int to = INT_MIN) const
	{
		if (from < 0) {
			from += g_end - g_start-1;
			if (from < 0) from = 0;
		}
		else if (from >= g_end - g_start) from = g_end - g_start-2;
		if (to == INT_MIN) to = from + 1;
		if (to < 0) {
			to += g_end - g_start-1;
			if (to < 0) to = 0;
		}
		else if (to >= g_end - g_start) to = g_end - g_start-2;
		if (from > to) {
			int t = from;
			from = to;
			to = t;
		}
		return GraphemeString(source, from + g_start, to + g_start + 2);
	}
	GraphemeString operator[](int i) const
	{
		if (i < 0 || i >= grapheme_length()-1) {
			return GraphemeString(new GraphemeString_letter(""), 0, 1);
		}
		return GraphemeString(source, i + g_start, i + g_start + 2);
	}

	GraphemeString deep_copy()
	{
		return GraphemeString(new GraphemeString_letter(*this), 0, size());
	}

	GraphemeString(const std::vector <GraphemeString> &o) {
		source = new GraphemeString_letter(o);
		g_start = 0; g_end = source->grapheme_to_codepoint_index.size() - 1;
		fill_hash();
	}
	GraphemeString(const uint8_t* s) :source(new GraphemeString_letter(s)), g_start(0), g_end(source->grapheme_to_codepoint_index.size() - 1) { fill_hash(); }
	GraphemeString(const char* s) :source(new GraphemeString_letter((const uint8_t*)s)), g_start(0), g_end(source->grapheme_to_codepoint_index.size() - 1)
	{
		fill_hash();
	}
//	GraphemeString(const char* s,SingletonEnum) :source(new GraphemeString_letter((const uint8_t*)s,Singleton)), g_start(0), g_end(source->grapheme_to_codepoint_index.size() - 1)
//	{
//		fill_hash();
//	}

	GraphemeString(const GraphemeString& o) :source(o.source), g_start(o.g_start), g_end(o.g_end), hash_value1(o.hash_value1), hash_value2(o.hash_value2) {  }
	GraphemeString(GraphemeString&& o) = default;
	//for windows!
	GraphemeString(const wchar_t* s) {
		std::unique_ptr<const uint8_t> ts((const uint8_t*)UnicodeToUTF8(s));
		source = new GraphemeString_letter(&*ts);
		g_start = 0;
		g_end = source->grapheme_to_codepoint_index.size()-1;
		fill_hash();
	}
	GraphemeString(const int32_t* s) {
		source = new GraphemeString_letter(s);
		g_start = 0;
		g_end = source->grapheme_to_codepoint_index.size() - 1;
		fill_hash();
	}

	GraphemeString back() const { return (*this)[size() - 1]; }
	GraphemeString front() const { return (*this)[0]; }

	GraphemeIterator begin() const;
	GraphemeIterator end() const;
	RGraphemeIterator rbegin() const;
	RGraphemeIterator rend() const;

	bool hash_lt(const GraphemeString& b) const {
		return  hash1() < b.hash1() || (hash1() == b.hash1() && (hash2() < b.hash2()));
	}

	bool hash_order_lt(const GraphemeString& b) const {
		return  hash1() < b.hash1() || (hash1() == b.hash1() && (hash2() < b.hash2() || (hash2() == b.hash2() && *this < b)));
	}

	bool hash_eq(const GraphemeString& o) const
	{
		return hash_value1 == o.hash_value1 && hash_value2 == o.hash_value2;
	}
	//for std::unordered_map
	//declare std::unordered_map<GraphemeString, sometype, GraphemeString::HashFunction> umap;
	//due to the std namespace hash declaration below the class, this should be unnecessary
	//std::unordered_map<GraphemeString, sometype> umap; should work
	struct HashFunction {
		std::size_t operator()(const GraphemeString& o) const { return (std::size_t)o.hash1(); }
	};

	//for map constructor
	//declare std::map<GraphemeString, sometype, GraphemeString::cmpByHashOrder> umap;
	//declare std::map<GraphemeString, sometype, GraphemeString::cmpByHashOnly> umap;
	// due to the < operator being defined this last one is redundant
	//declare std::map<GraphemeString, sometype, GraphemeString::cmp> umap;
	//declare std::map<GraphemeString, sometype> umap; is equivalent
	struct cmpByHashOrder {
		bool operator()(const GraphemeString& a, const GraphemeString& b) const {
			return  a.hash_order_lt(b);
		}
	};

	struct cmpByHashOnly {
		bool operator()(const GraphemeString& a, const GraphemeString& b) const {
			return  a.hash_lt(b);
		}
	};

	struct cmp {
		bool operator()(const GraphemeString& a, const GraphemeString& b) const {
			return  a<b;
		}
	};


	bool operator==(const GraphemeString& o) const
	{
		//if (size() ==0 && o.size()==0) return true;
		if (hash_value1 != o.hash_value1 || hash_value2 != o.hash_value2) return false;

		//the probability of the rest of this being anything more than a waste of time is 1:10^38 small.
		if (size() != o.size()) return false;
		for (int i = codepoint_start_slice_index(), j = o.codepoint_start_slice_index(); i < codepoint_end_slice_index(); ++i, ++j) {
			int32_t a = source->codepoint_buffer[i];
			int32_t b = o.source->codepoint_buffer[j];
			if (a != b) {
				return false;
			}
//			if (source->codepoint_buffer[i] != o.source->codepoint_buffer[j]) return false;
		}
		return true;
	}
	bool operator<(const GraphemeString& o) const
	{
		int min_size = size() < o.size() ? size() : o.size();
		if (min_size == 0) {
			if (o.size() > 0) return true;
			return false;
		}
		for (int i = 0; i < min_size-1; ++i) {
			int32_t left = source->codepoint_buffer[i + codepoint_start_slice_index()];
			int32_t right = o.source->codepoint_buffer[i + o.codepoint_start_slice_index()];
			if (left != right) {
				return left < right;
			}
		}
		return size() < o.size();
	}

	bool operator!=(const GraphemeString& o) const { return !(*this == o); }
	bool operator>=(const GraphemeString& o) const { return !(*this < o); }
	bool operator<=(const GraphemeString& o) const { return !(o < *this); }
	bool operator>(const GraphemeString& o) const { return o < *this; }

};

template<>
struct std::hash<GraphemeString>
{
	std::size_t operator()(GraphemeString const& s) const noexcept
	{
		return (size_t)s.hash1();
	}
};

//for std::unordered_map ??and others?


inline std::ostream& operator<<(std::ostream& os, const GraphemeString& o) {
	std::unique_ptr<uint8_t[]> buf(new uint8_t[o.byte_length() + 1]);
	o.fill_utf8(&buf[0]);

	return os << &buf[0];
}

class GraphemeStringBuilder {
	std::vector<GraphemeString> list;
public:
	GraphemeStringBuilder& operator<<(const GraphemeString &o)
	{
		list.push_back(o);
		return *this;
	}
	operator GraphemeString() const {
		return GraphemeString(list);
	}
	GraphemeString build() const {
		return GraphemeString(list);
	}
};

class GraphemeIterator {
	GraphemeString s;
	int pos;
	friend class GraphemeString;
	GraphemeIterator(const GraphemeString& s, int p = 0) :s(s), pos(p) {}
public:
	GraphemeString operator *() const {
		return s[pos];
	}
	GraphemeString operator [](int i) const {
		return s[pos + i];
	}
	GraphemeIterator& operator ++()
	{
		++pos;
		return *this;
	}
	GraphemeIterator& operator --()
	{
		--pos;
		return *this;
	}
	GraphemeIterator operator ++(int)
	{
		GraphemeIterator temp = *this;
		++pos;
		return temp;
	}
	GraphemeIterator operator --(int)
	{
		GraphemeIterator temp = *this;
		--pos;
		return temp;
	}
	bool operator == (const GraphemeIterator& o) const
	{
		return pos == o.pos;
	}
	bool operator != (const GraphemeIterator& o) const
	{
		return pos != o.pos;
	}
};
inline GraphemeIterator GraphemeString::begin() const { return GraphemeIterator(*this, 0); }
inline GraphemeIterator GraphemeString::end() const { return GraphemeIterator(*this, size()-1); }
class RGraphemeIterator {
	GraphemeString s;
	int pos;
	friend class GraphemeString;
	RGraphemeIterator(const GraphemeString& s, int p = 0) :s(s), pos(s.size() - 1 - p) {}
public:
	GraphemeString operator *() const {
		return s[pos];
	}
	GraphemeString operator [](int i) const {
		return s[pos - i];
	}
	RGraphemeIterator& operator ++()
	{
		--pos;
		return *this;
	}
	RGraphemeIterator& operator --()
	{
		++pos;
		return *this;
	}
	RGraphemeIterator operator ++(int)
	{
		RGraphemeIterator temp = *this;
		--pos;
		return temp;
	}
	RGraphemeIterator operator --(int)
	{
		RGraphemeIterator temp = *this;
		++pos;
		return temp;
	}
	bool operator == (const RGraphemeIterator& o) const
	{
		return pos == o.pos;
	}
	bool operator != (const RGraphemeIterator& o) const
	{
		return pos != o.pos;
	}
};
inline RGraphemeIterator GraphemeString::rbegin() const { return RGraphemeIterator(*this, 1); }
inline RGraphemeIterator GraphemeString::rend() const { return RGraphemeIterator(*this, size()); }
#endif
