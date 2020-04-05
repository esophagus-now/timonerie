#ifndef SYMTAB_H
#define SYMTAB_H 1

/* Just like with the pollfd_array stuff, I don't expect the symbol tables
 * to be searched mare than a few times per second, so I really don't care
 * about speed. I only care about simplicity.
 * 
 * By the way, I don't expect the user to have more than 200 symbols at any
 * one time, so I also don't care to much about space.
 * */
 
#include "pollfd_array.h"

#define MAX_SYM_SIZE 64
#define MAX_SYM_DATA 64
typedef struct _symtab_entry {
    char sym[MAX_SYM_SIZE];
    char dat[MAX_SYM_DATA];
    int dat_len;
} symtab_entry;

typedef struct _symtab {
    symtab_entry *entries;
    int nents;
    int cap;
    
    //Error information
    char const* error_str;
} symtab;

//Returns a malloc'ed symtab, or NULL on error
symtab* new_symtab(int howmany);

//Frees memory allocated in *s. Gracefully ignores NULL input
void del_symtab(symtab *s);

//Adds new entry to the end of the symbol table. Makes internal copies of 
//each input using malloc. Returns 0 on success, -1 on error (and sets 
//s->error_str). NOTE: returns -2 if p was NULL. 
int symtab_append(symtab *s, char *sym, void *dat, int dat_len);

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets s->error_str). By the way, also makes sure that no duplicate syms
//are added, although will update data if the sym is found in the array
//NOTE: returns -2 if s was NULL. 
//This function is slow as hell, but who cares?
int symtab_append_nodup(symtab *s, char *sym, void *dat, int dat_len);

//Simple deletion by overwriting the element at ind with a copy of the last
//element in the array, then decreasing the array's size. Returns 0 on 
//success, -1 on error (and sets s->error_str appropriately). NOTE: returns
//-2 if s was NULL
int symtab_remove_at_index(symtab *s, int ind);

//Tries to remove the symtab_entry pointed to by ent from the array. 
//Returns 0 on success, -1 on error (and sets s->error_str if possible).
//NOTE: returns -2 if s is NULL
int symtab_array_remove(symtab *s, symtab_entry *ent);

//Looks up the entry under sym. Returns a pointer to the symtab_entry struct
//on success (see the sym_dat macro) or NULL on error, and sets 
//s->error_str (if possible)
//Slow as hell, but who cares?
symtab_entry* symtab_lookup(symtab *s, char *sym);

//You don't have to use this, just makes code a little cleaner
#define sym_dat(ent, type) ((type)((ent)->dat))

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
extern char const *const SYMTAB_SUCC; // = "success";
extern char const *const SYMTAB_OOM; // = "out of memory";
extern char const *const SYMTAB_OOB; // = "out of bounds";
extern char const *const SYMTAB_NOT_FOUND; // = "symbol not found";
extern char const *const SYMTAB_NULLARG; // = "received NULL argument but expected non-NULL";
extern char const *const SYMTAB_TOO_BIG; // = "data too large";
extern char const *const SYMTAB_INVALID; // = "invalid argument";
extern char const *const SYMTAB_NOT_MEMB; // = "not a member of the symbol table";

#endif
