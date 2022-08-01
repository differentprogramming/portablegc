#ifndef _HASH_TABLE_
#define _HASH_TABLE_
#include <vector>
#include <exception>
#include <algorithm>
#include "spooky.h"
#include <cmath>
/* A simple hash table with the following assumptions:
1) they keys are strings up to length MAX_HASH_STRING_SIZE
a) the keys handed to the routines are constants and it's not up to the routines to own or clean them up.
b) strings will be copied into keys, and the Hash functions will manage that memory.
2) The table length has to be a power of 2 and will grow as necessary to mostly avoid collisions
3) the code relies on spooky hash

The hash table uses external lists for collisions but the table stays at least as big as twice the number of elements so there shouldn't be too many collisions.
The elements of the hash table are not pointers they're embedded Entry structs, so not very much external calls to new and delete will be necessary.
If you really want to avoid new and delete, then you can change the test at the end of HashInsert to expand earlier.

The original C version was probably a bit faster on expand and delete because it assumed that memory can be zeroed and copied.
This version uses constructors and destructors and std::move properly instead.
By the way, you should define a move constructor for your type that releases ownership of memory, if it's complicated enough to hold some.
It should also have an assignment operator.
 */

/* for safety string operations should have a maximum size, they truncate after that. */
#define MAX_HASH_STRING_SIZE 1051

#define INITIAL_HASH_SIZE 1024

enum HashInsertStatus { HS_Inserted, HS_Found };


template <typename T>
class IntrusiveHashEntry {
public:
	IntrusiveHashEntry() :key(nullptr), prev(nullptr), next(nullptr),
		hash_value(0) {}
	IntrusiveHashEntry<T> &operator=(IntrusiveHashEntry<T> &&other) {
		value = std::move(other.value);
		key = std::move(other.key);
		other.key = nullptr;
		prev = std::move(other.prev);
		next = std::move(other.next);
		hash_value = std::move(other.hash_value);
		return *this;
	}
	~IntrusiveHashEntry() {
		free(key);
	}
	T value;
	char * key;
	IntrusiveHashEntry<T> *prev;
	IntrusiveHashEntry<T> *next;
	uint64_t hash_value;
};
template <typename T>
class IntrusiveHashTable {
public:

	struct Iterator {
		IntrusiveHashTable<T> *h;
		IntrusiveHashEntry<T> *cur_entry;
		int cur_slot;
		int pos;
		Iterator(IntrusiveHashTable<T> *h);
		bool reset();
		inline bool at_end() const { return pos == h->number_of_entries; }
		/* returns true if the iterator is on an item, false when it's past all the items or if there are none */
		bool operator++();
		/* leaves the iterator past what it deleted.  Returns false if the
		 * element deleted was at the end of the table or if the table was or is
		 * empty */
		bool delete_here();
	};
private:
	bool find_u(IntrusiveHashEntry<T>**ptr, uint64_t* hash_value, const char *s);
	void expand();
	void expand_insert(uint64_t hash_value, char *s, const T &v);
	bool HashDeleteU(IntrusiveHashEntry<T> *p, bool save_key);
	bool _DeleteAtHashIterator(Iterator *hi, bool save_key);
	IntrusiveHashTable<T> &operator=(IntrusiveHashTable<T> &&other) //for expanding.  Assumes destination is empty
	{
		delete[] data;
		data = other.data;
		other.data = nullptr;
		number_of_entries = other.number_of_entries;
		other.number_of_entries = 0;
		HASH_SIZE = other.HASH_SIZE;
		seed = other.seed;
		return *this;
	}
public:
    IntrusiveHashTable(int initial_size = INITIAL_HASH_SIZE, uint64_t _seed= 0x5f71203b);
    ~IntrusiveHashTable();
	/* returns a IntrusiveHashEntry if the element is found, NULL otherwise */
	IntrusiveHashEntry<T>* find(const char *s);
	IntrusiveHashEntry<T>* find_with_precomputed_hash(const char *s, uint64_t hash);
	bool delete_key(const char *s);
	int number_of_entries;
	int HASH_SIZE;
	uint64_t seed;
	IntrusiveHashEntry<T> *data;
    inline bool empty(int at) { return data[at].key == nullptr; }
	/* returns HS_Inserted if no such key already existed in the table
	 * returns HS_Found if that key was already in the table.
	 * if replace is set then the value is replaced even if the key was already in the table.
	 * if replace is not set and the key is already in the table, then the value is not changed.
	 *    in the case of a collision, then whichever value is not needed is cleaned up with CleanUpHashValue()
	 *    if you need to avoid that, then test for a collision with HashFind before you call HashInsert.
	 * if ptr is not NULL then *ptr is set to point at the new or found entry
	 * note than the key is copied if it's placed in the table
	 */
	HashInsertStatus insert(IntrusiveHashEntry<T>** ptr, const char *s, const T &v, bool replace);
};

template<typename T>
bool IntrusiveHashTable<T>::Iterator::reset() {
	int i;
	pos = 0;
	cur_entry = nullptr;
	cur_slot = 0;
	for (i = 0; i < h->HASH_SIZE; ++i)
		if (h->data[i].key != nullptr) break;
	if (i == h->HASH_SIZE) return false;//table is empty
	cur_slot = i;
	cur_entry = &h->data[i];
	return true;
}

template<typename T>
IntrusiveHashTable<T>::Iterator::Iterator(IntrusiveHashTable<T> *_h) {
	h = _h;
	reset();
}

template <typename T>
bool IntrusiveHashTable<T>::Iterator::operator ++() {
	if (at_end())
		return false;
	++pos;
	if (at_end()) {
		cur_entry = nullptr;
		cur_slot = 0;
		return false;
	}
	if (nullptr != cur_entry->next) {
		cur_entry = cur_entry->next;
	}
	else {
		do {
			++cur_slot;
		} while (h->data[cur_slot].key == nullptr);
		cur_entry = &h->data[cur_slot];
	}
	return true;
}

//returns true if p is now the pointer to the next entry
template <typename T>
bool IntrusiveHashTable<T>::HashDeleteU(IntrusiveHashEntry<T> *p, bool save_key) {
	--number_of_entries;
	if (save_key) {
		p->key = nullptr;
	} 
	if (p->prev == nullptr) {//inline entry
		if (p->next == nullptr) { // no next entry
			p->~IntrusiveHashEntry<T>();
			new ((void *)p) IntrusiveHashEntry<T>();
		}
		else {
			IntrusiveHashEntry<T> *next = p->next;
			IntrusiveHashEntry<T> *nn = next->next;
			*p = std::move(*next);
			p->prev = nullptr;
			if (nullptr != nn) nn->prev = p;
			delete next;
			return true;
		}
	}
	else {
		p->prev->next = p->next;
		if (nullptr != p->next) {
			p->next->prev = p->prev;
			p->next = nullptr;
		}
		delete p;
	}
	return false;
}

template <typename T>
bool IntrusiveHashTable<T>::_DeleteAtHashIterator(IntrusiveHashTable<T>::Iterator *hi, bool save_key)
{
	if (hi->at_end()) return false;
	IntrusiveHashEntry<T> *doomed = hi->cur_entry;
	int slot = hi->cur_slot;

	++*hi;
	if (hi->h->HashDeleteU(doomed, save_key)) {
		hi->cur_entry = doomed;
		hi->cur_slot = slot;
	}
	hi->pos--;
	return !hi->at_end();
}

template <typename T>
bool IntrusiveHashTable<T>::Iterator::delete_here() {
	return h->_DeleteAtHashIterator(this, false);
}
template <typename T>
IntrusiveHashTable<T>::IntrusiveHashTable(int initial_hash_size,uint64_t _seed) {
	number_of_entries = 0;
	seed = _seed;
	HASH_SIZE = initial_hash_size;
	data = new IntrusiveHashEntry<T>[HASH_SIZE];
}
//on true, *ptr is the found entry
//on false *ptr is either the empty entry found or the full entry that could be expanded to point to a new entry
template <typename T>
bool IntrusiveHashTable<T>::find_u(IntrusiveHashEntry<T>**ptr, uint64_t* hash_value, const char * s)
{
	uint64_t hs;

	//can be used both to fill in an unknown hash value or to use a precalculated hash value 
	if (hash_value != nullptr) {
		if (*hash_value == 0) {
			hs = hash_string_with_seed(s, seed);
			*hash_value = hs;
		}
		else hs = *hash_value;
	} else hs = hash_string_with_seed(s, seed);
	
	if (hash_value != nullptr) *hash_value = hs;
	int at = (int)hs&(HASH_SIZE - 1);
	IntrusiveHashEntry<T>* p = &data[at];
	if (ptr != nullptr) *ptr = p;
	if (empty(at)) return false;
	while (p != nullptr && (p->hash_value != hs || 0 != strncmp(s, p->key, MAX_HASH_STRING_SIZE))) {
		if (ptr != nullptr) *ptr = p;
		p = p->next;
	}
	if (p != nullptr) {
		if (ptr != nullptr) *ptr = p;
		return true;
	}
	return false;
}

template <typename T>
IntrusiveHashEntry<T>* IntrusiveHashTable<T>::find(const char *s)
{
	IntrusiveHashEntry<T>* p;
	if (find_u(&p, nullptr, s)) {
		return p;
	}
	return nullptr;
}
template <typename T>
IntrusiveHashEntry<T>* IntrusiveHashTable<T>::find_with_precomputed_hash(const char *s, uint64_t hash)
{
	IntrusiveHashEntry<T>* p;
	if (find_u(&p, hash, s)) {
		return p;
	}
	return nullptr;
}

template <typename T>
HashInsertStatus IntrusiveHashTable<T>::insert(IntrusiveHashEntry<T>** ptr, const char *s, const T &v, bool replace)
{
	IntrusiveHashEntry<T> *p;
	uint64_t hash_value=0;
	if (ptr == NULL) ptr = &p;

	if (find_u(ptr, &hash_value, s)) {
		if (replace) {
			(*ptr)->value = v;
		}
		return HS_Found;
	}
	else {
		IntrusiveHashEntry<T> *prev = *ptr;
		if (prev->key == NULL)
		{
			prev = NULL;
		}
		else {
			(*ptr)->next = new IntrusiveHashEntry<T>();
			*ptr = (*ptr)->next;
		}
		(*ptr)->prev = prev;
		(*ptr)->key = strdup(s);
		(*ptr)->value = v;
		(*ptr)->hash_value = hash_value;
		if (++number_of_entries >= HASH_SIZE >> 1) expand();
		return HS_Inserted;
	}
}

/* Assumes that the value is not in the table already and it copies the key without dupicating it. And it is handed the hash value instead of computing it.
 */
template <typename T>
void IntrusiveHashTable<T>::expand_insert(uint64_t hash_value, char *s, const T& v)
{
	int at = (int)hash_value&(HASH_SIZE - 1);
	IntrusiveHashEntry<T>* p = &data[at];
	if (!empty(at)) {
		while (p->next != nullptr) p = p->next;
	}

	IntrusiveHashEntry<T> *prev = p;
	if (prev->key == nullptr)
	{
		prev = nullptr;
	}
	else {
		p->next = new IntrusiveHashEntry<T>();
		p = p->next;
	}
	p->prev = prev;
	p->key = s;
	p->value = v;
	p->hash_value = hash_value;
	++number_of_entries;
}

template <typename T>
void IntrusiveHashTable<T>::expand()
{
	IntrusiveHashTable temp(HASH_SIZE << 1,seed);
	Iterator itr(this);
	while (!itr.at_end()) {
		temp.expand_insert(itr.cur_entry->hash_value, itr.cur_entry->key, itr.cur_entry->value);
		_DeleteAtHashIterator(&itr, true);
	};

	*this = std::move(temp);
}

template <typename T>
bool IntrusiveHashTable<T>::delete_key(const char *s)
{
	IntrusiveHashEntry<T> *p;
	if (!find_u(&p, nullptr, s)) {
		return false;
	}
	HashDeleteU(p, false);
	return true;
}

template <typename T>
IntrusiveHashTable<T>::~IntrusiveHashTable()
{
	if (number_of_entries > 0) {
		Iterator itr(this);
		while (!itr.at_end()) {
			itr.delete_here();
		};
	}
	delete[] data;
	data = nullptr;
}


/* Inserting or deleting from the hash table while you're iterating on it, makes the state of the iterator invalid and will cause bugs
 * except that it's ok to use DeleteAtHashIterator which will leave the iterator pointing to the next item if there is one.
 * note that the iterator can only move forward, although the data structures allow for backward motion as well, it's just not
 * implemented.
 */


int high_bit_number(int i);

struct Random64
{
	uint64_t a;
	uint64_t b;
	uint64_t c;
	uint64_t d;

	inline uint64_t random_value() {
		uint64_t e = a - rot64(b, 23);
		a = b ^ rot64(c, 16);
		b = c + rot64(d, 11);
		c = d + e;
		d = e + a;

		return d;
	}


	Random64(uint64_t seed, bool dont_bother = false);
};
/* ManagedStrings and String allocators are not trying to solve the usual problems of ownership and sharing.  They exist in order to minimize the calls to memory allocation functions.
  It is assumed that the liftime of a string isn't affected by the existence of ManagedString objects that reference those strings, the lifetime is up to the string allocator.
  There are two kinds of string allocators, StringRingAllocator<n> which exists to stream through strings, only keeping the previous n strings.    StringSourceAllocator exists to buffer all of the constant strings 
  in a program.  Every string added to it exists for the life of the compilation. It simply exists to aggrigate memory allocation calls.
 */
class StringAllocatorBase;
struct ManagedString
{
	StringAllocatorBase *allocator;
	int len;
	int block_offset;
	int string_index_within_block;
	int block_index;
	ManagedString() :allocator(nullptr),len(0) {}
};
//generates a new string and returns it
//not called if original needs no processing
typedef const char *(clean_up_fn)(int &destlen,char *dest,const char *&err, const char *source, int len);
class StringAllocatorBase
{
public:
	StringAllocatorBase() {}
	virtual ~StringAllocatorBase(){}

};


class PerfectHashTable {
public:
	Random64 rnd;
	int total_tokens;
	uint64_t seed;
	const char **names;
	std::vector<int> keylen;
	std::vector<uint64_t> entries;

	std::vector<int> token_number_per_entry;
	//tokens listed from low to high
	std::vector<int> by_length;
	std::vector<int> length_index;//from token to postition in by_length vector

	int16_t single_char_tokens[256];
	uint8_t max_token_length[256];
	int16_t double_char_tokens[97 * 97];

	bool calc_double_char_index(int & dest, const char*source)
	{
		const unsigned char *s = (const unsigned char *)source;
		if (s[0] < 32 || s[0]>127 || s[1] < 32 || s[1]>127) return false;
		dest = (s[0] - 32) + (s[1] - 32)*(128 - 32);
		return true;
	}
	int HASH_SIZE;
	//true on is a keyword, the token number is in token_number
	//false on not found, then the hash is in hash
	bool find(int &token_number, uint64_t &hash, const char *name, int len = 0);

	PerfectHashTable(const char **keywords, int num_keywords=0, uint64_t _seed = 0, int known_size = 0);
};

class SolidAsciiTokenizer;

struct LexToken
{
	LexToken() :token_number(-1), ignore(false), value_tag(TVT_NONE), line(0), line_offset(0), len(0), filename(-1), name(-1) {}
	SolidAsciiTokenizer * parent;
	//-1 for a lexical error
	int token_number;
	//ignore whitespace and comments
	bool ignore;
	//whitespace, keywords, comments and lexical errors have NONE for value
	enum LEX_TK_VALUE_TYPES { TVT_NONE, TVT_INT, TVT_DOUBLE, TVT_STRING, TVT_ID };
	LEX_TK_VALUE_TYPES value_tag;
	union {
		uint64_t int_value;//- is a separate token 
		double double_value;
		clean_up_fn * clean_up_data; //clean up version of the id or string, null if no processing necessary
	};
	int line;
	int line_offset;
	int len;
	int filename; //as a number so that buffer can be a vector
	int name; //position in buffer of scanned token - an int so that the buffer can be a vector.
	uint64_t hash;
	double as_double() {
		if (value_tag == TVT_INT) return int_value;
		else if (value_tag == TVT_DOUBLE) return double_value;
		else throw(std::invalid_argument("token is not a number"));
	}
	const char * get_text();
	void get_text(char *buf, int buflen);
};


const char *process_string(const char *&err, const char *source, int len);

#define TOKEN_CONTEXT_LENGTH 4



enum TOKENS {
	TK_ARRAY,	//0 array
	TK_AND,		//and
	TK_AS,		//as
	TK_AST,		//ast
	TK_ATOM,	//atom
	TK_BECOME,	//become
	TK_BIG,		//big
	TK_BIOP,	//biop
	TK_BOXED,	//boxed
	TK_BREAK,	//break
	TK_BYTE,	//10 byte
	TK_CAST,	//cast
	TK_CALL,	//call
	TK_CASE,	//case
	TK_CLASS,	//class
	TK_CUT,		//cut
	TK_DECLARE,	//declare
	TK_DELETE,	//delete
	TK_DEFAULT,	//default
	TK_DO,		//do
	TK_CONTINUE,//20 continue
	TK_CONSTANT,//constant
	TK_CONTINUATION,//continuation
	TK_CTREE,	//ctree
	TK_ELSE,	//else
	TK_ELSEIF,	//elseif
	TK_ENDFUNCTION,//endfunction
	TK_ENDGENERATOR,//endgenerator
	TK_ENDSWITCH,	//endswitch
	TK_ENDIF,		//endif
	TK_ENDWHILE,	//30 endwhile
	TK_FUNCTION,	//function
	TK_GENERATOR,	//generator
	TK_IF,			//if
	TK_INDEX,		//index
	TK_INTEGER,		//integer
	TK_INTERFACE,	//interface
	TK_LIST,		//list
	TK_LOGICAL,		//logical	
	TK_MATCH,		//match
	TK_MAYBE,		//40 maybe
	TK_MOD,			//mod
	TK_NEW,			//new
	TK_NIL,			//nil
	TK_NO,			//no
	TK_NOT,			//not
	TK_OBJECT,		//object
	TK_OF,			//of
	TK_OR,			//or
	TK_ONE,			//one
	TK_POINTER,		//50 pointer
	TK_POST,		//post
	TK_PRE,			//pre
	TK_REAL,		//real
	TK_RECORD,		//record
	TK_RETURNING,	//returning
	TK_SEND,		//send
	TK_SIZEOF,		//sizeof
	TK_STRING,		//string
	TK_SUPER,		//super
	TK_SWITCH,		//60 switch
	TK_TABLE,		//table
	TK_THEN,		//then
	TK_TO,			//to
	TK_UNIFY,		//unify
	TK_UNTIL,		//until
	TK_VALUE,		//value
	TK_VAR,			//var
	TK_WHERE,		//where
	TK_WHETHER,		//whether
	TK_WHILE,		//70 while
	TK_BXOR,		//bxor
	TK_YES,			//yes
	TK_MINUS,		//-
	TK_ASTERIX,		//*
	TK_AMPERSAND,	//&
	TK_EXCLAMATION,	//!
	TK_TILDE,		//~
	TK_PLUSPLUS,	//++
	TK_MINUSMINUS,	//--
	TK_EQUAL,		//80 =
	TK_PLUSEQ,		//+=
	TK_MINUSEQ,		//-=
	TK_ASTERIXEQ,	//*=
	TK_SLASHEQ,		// /=
	TK_MODEQ,		//mod=
	TK_LTLTEQ,		//<<=
	TK_GTGTEQ,		//>>=
	TK_BANDEQ,		//band=
	TK_BOREQ,		//bor=
	TK_BXOREQ,		//90 bxor=
	TK_CAROT,		//^
	TK_LE,			//<=
	TK_GE,			//>=
	TK_LT,			//<
	TK_GT,			//>
	TK_EQEQ,		//==
	TK_NOTEQ,		//not=
	TK_LTEQGT,		//<=>
	TK_DOTDOT,		//..
	TK_PERIOD,		//100 .
	TK_PIPE,		//|
	TK_GTGT,		//>>
	TK_LTLT,		//<<
	TK_PLUS,		//+
	TK_SLASH,		// /
	TK_QMARKEQ,		//?=
	TK_QMARK,		//?
	TK_BSLASH,		// \ blash
	TK_HASH,		//#
	TK_HASHPIPE,	//110 #|
	TK_BQUOTE,		//`
	TK_COMMA,		//,
	TK_COLONCOLON,	//::
	TK_LP,			//(
	TK_RP,			//)
	TK_LB,			//[
	TK_RB,			//]
	TK_LBRACE,		//{
	TK_RBRACE,		//}
	TK_SINGLEQUOTE,	//120 '
	TK_COLON,		//:
	TK_SEMICOLON,	//;
	TK_IDENT, //synthetic tokens come after the ones listed as keywords
	TK_INTEGER_CONST,
	TK_REAL_CONST,
	TK_STRING_CONST,
	TK_WHITESPACE,
	TK_COMMENT,
	TK_MACROID,
	TK_EOF,//130
	TK_NUM_TOKENS
};
/*
	atom constants start with $
	ident: is for message selectors
	line comment %
	multiline comment, same as c
	multisend start ::>
	multisend continue :>
	cascade |>
	=> maps to
	visible
	grammer used | & ^ ~ for band bor bxor bnot
	= assign
	== eq
	private
	predicate endpredicate
	endsavecont
	now
	oncontinue
	savecontinue
	continuable
	catch
	endclass
	module 
	import
	require
	export

		: simple_type
	| CLASS specify
	| ARRAY NUM_INT? (OF type)?
	| TABLE (OF type)?
	| LIST (OF type)?
	| CTREE (OF type)?
	| POINTER TO type
	| RECORD specify

*/


enum CTOKENS {
	CTOK_AUTO,//0
	CTOK_BREAK,
	CTOK_CASE,
	CTOK_CHAR,
	CTOK_CONST,
	CTOK_CONTINUE,
	CTOK_DEFAULT,
	CTOK_DO,
	CTOK_DOUBLE,
	CTOK_ELSE,
	CTOK_ENUM, //10
	CTOK_EXTERN,
	CTOK_FLOAT,
	CTOK_FOR,
	CTOK_GOTO,
	CTOK_IF,
	CTOK_INLINE,
	CTOK_INT,
	CTOK_LONG,
	CTOK_REGISTER,
	CTOK_RESTRICT,//20
	CTOK_RETURN,
	CTOK_SHORT,
	CTOK_SIGNED,
	CTOK_SIZEOF,
	CTOK_STATIC,
	CTOK_STRUCT,
	CTOK_SWITCH,
	CTOK_TYPEDEF,
	CTOK_UNION,
	CTOK_UNSIGNED,//30
	CTOK_VOID,
	CTOK_VOLATILE,
	CTOK_WHILE,
	CTOK__ALIGNAS,
	CTOK__ALIGNOF,
	CTOK__ATOMIC,
	CTOK__BOOL,
	CTOK__COMPLEX,
	CTOK__GENERIC,
	CTOK__IMAGINARY,
	CTOK__NORETURN,
	CTOK__STATIC_ASSERT,
	CTOK__THREAD_LOCAL,
	CTOK_LBRACE,
	CTOK_RBRACE,
	CTOK_LP,
	CTOK_RP,
	CTOK_LBRACKET,
	CTOK_RBRACKET,
	CTOK_PERIOD,
	CTOK_MEMBERSELECT,
	CTOK_PLUSPLUS,
	CTOK_MINUSMINUS,
	CTOK_AMPERSAND,
	CTOK_ASTERIX,
	CTOK_PLUS,
	CTOK_MINUS,
	CTOK_TILDE,
	CTOK_EXCLAMATION,
	CTOK_SLASH,
	CTOK_PERCENT,
	CTOK_LTLT,
	CTOK_GTGT,
	CTOK_LT,
	CTOK_GT,
	CTOK_LE,
	CTOK_GE,
	CTOK_EQEQ,
	CTOK_NE,
	CTOK_CAROT,
	CTOK_PIPE,
	CTOK_AMPAMP,
	CTOK_PIPEPIPE,
	CTOK_QMARK,
	CTOK_COLON,
	CTOK_SEMICOLON,
	CTOK_DOTDOTDOT,
	CTOK_EQ,
	CTOK_ASTERIXEQ,
	CTOK_SLASHEQ,
	CTOK_PERCENTEQ,
	CTOK_PLUSEQ,
	CTOK_MINUSEQ,
	CTOK_LTLTEQ,
	CTOK_GTGTEQ,
	CTOK_AMPEQ,
	CTOK_CAROTEQ,
	CTOK_PIPEEQ,
	CTOK_COMMA,
	CTOK_POUND,
	CTOK_POUNDPOUND,
	CTOK_LBRACEDIGRAPH,
	CTOK_RBRACEDIGRAPH,
	CTOK_LBRACKETDIGRAPH,
	CTOK_RBRACKETDIGRAPH,
	CTOK_HASHDIGRAPH,
	CTOK_HASHHASHDIGRAPH,
	CTOK_IDENT, //synthetic tokens come after the ones listed as keywords
	CTOK_INTEGER,
	CTOK_REAL,
	CTOK_STRING,
	CTOK_WHITESPACE,
	CTOK_COMMENT,
	CTOK_EOF,
};


//inherited because there is only one Perfect HashTable, but there can be lots of kinds of tokenizers
class SolidAsciiTokenizer : public PerfectHashTable
{
public:
	//returns the longest matching token
	//if none exists then it returns false and the hash of a single character or id or number or string 
	//bool find_longest(int &token_number, uint64_t &hash, const char *name, int &len);

	//character classes
	//alpha
	//single ( ) { } [ ] ; , ' ` & ! * ~ | \ ^
	//space
	//digit
	//punct - _ " ' % single
	//_
	//upper
	//lower
	//"
	//%
	//@
	//
	//note, special case for some words ended by =

	const int16_t CCALPHA = 1, CCSPACE = 2, CCDIGIT = 4, CCPUNCT = 8, CC_ = 16, CCUPPER = 32, CCLOWER = 64, CCDQUOTE = 128, CCPERCENT = 256, CCAT = 512, CCHEX =1024;

	static int16_t char_classes[256];

	//struct TokenizingContext
	//{
	bool eof;
		int number_of_tokens_parsed;

		//ring buffer of tokens
		int short_token_context_position;
		LexToken short_token_context[TOKEN_CONTEXT_LENGTH];

		int source_pos;
		std::vector<char> source;
		int filenames_pos;
		int line;
		int line_offset;
		bool do_c;
		std::vector<wchar_t> filenames; //the filename being read and also any included files. 
	//} tokenizing_context;
		void set_to_beginning_of_file()
		{
			source_pos=0;
			line = 0;
			line_offset = 0;
			eof = false;
		}
		LexToken &cur_token()
		{
			return short_token_context[short_token_context_position];
		}
		void inc_token()
		{
			++number_of_tokens_parsed;
			short_token_context_position = (1 + short_token_context_position)&(TOKEN_CONTEXT_LENGTH - 1);
		}
		void dec_token()
		{
			--number_of_tokens_parsed;
			short_token_context_position = (short_token_context_position-1)&(TOKEN_CONTEXT_LENGTH - 1);
		}

		//get a token n positions back, -1 if not available
		//use 0 to get the current token
		int token_pos_minus(int n) {
			if (number_of_tokens_parsed <= n || n >= TOKEN_CONTEXT_LENGTH) return -1;
			return (short_token_context_position -n)&(TOKEN_CONTEXT_LENGTH - 1);
		}
	//updates TokenizingContext
	//returns false when done;
	//throws on errors
	bool tokenize(bool skip_whitespace_and_comments = true);

	//set filename and source externally
	SolidAsciiTokenizer(bool do_c, const char **keywords, int num_keywords = 0, uint64_t _seed = 0, int known_size = 0);
};
#endif

