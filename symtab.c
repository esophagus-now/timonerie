#include <stdlib.h>
#include <string.h>
#include "symtab.h"

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
char const *const SYMTAB_SUCC = "success";
char const *const SYMTAB_OOM = "out of memory";
char const *const SYMTAB_OOB = "out of bounds";
char const *const SYMTAB_NOT_FOUND = "symbol not found";
char const *const SYMTAB_NULLARG = "received NULL argument but expected non-NULL";
char const *const SYMTAB_TOO_BIG = "data too large";
char const *const SYMTAB_INVALID = "invalid argument";
char const *const SYMTAB_NOT_MEMB = "not a member of the symbol table";

//Returns a malloc'ed symtab, or NULL on error
symtab* new_symtab(int howmany) {
    //Sanity check on input
    if (howmany < 0) return NULL;
    
    symtab *ret = malloc(sizeof(symtab));
    if (!ret) return NULL;
    
    ret->entries = malloc(howmany * sizeof(symtab_entry));
    if (ret->entries == NULL) {
        free(ret);
        return NULL;
    }
    
    ret->nents = 0;
    ret->cap = howmany;
    
    ret->error_str = SYMTAB_SUCC;
    return ret;
}

//Frees memory allocated in *s. Gracefully ignores NULL input
void del_symtab(symtab *s) {
    if (!s) return;
    
    if (s->entries != NULL) free(s->entries);
    
    free(s);
}

//Returns 0 on success, else -1 and sets s->error_str
static int make_space_for_one_more(symtab *s) {
    if (s->nents < s->cap) return 0;
    
    symtab_entry *res = realloc(s->entries, s->cap * 2 * sizeof(symtab_entry));
    
    if (res == NULL) {
        s->error_str = SYMTAB_OOM;
        return -1;
    } else {
        s->entries = res;
    }
    
    s->cap *= 2;
    return 0;
}

//Adds new entry to the end of the symbol table. Makes internal copies of 
//each input using malloc. Returns 0 on success, -1 on error (and sets 
//s->error_str). NOTE: returns -2 if p was NULL. 
int symtab_append(symtab *s, char *sym, void *dat, int dat_len) {
    //Sanity check on input
    if (s == NULL) {
        return -2; //This is all we can do
    }
    
    if (sym == NULL) {
        s->error_str = SYMTAB_NULLARG;
        return -1;
    }
    
    if (dat_len < 0) {
        s->error_str = SYMTAB_INVALID;
        return -1;
    }
    
    if (dat_len > MAX_SYM_DATA) {
        s->error_str = SYMTAB_TOO_BIG;
        return -1;
    }
    
    //Make sure there is enough room for the new entry
    int rc = make_space_for_one_more(s);
    if (rc < 0) {
        return -1; //make_space_for_one_more already set error_str
    }
    
    symtab_entry *ent = s->entries + s->nents++;
    strncpy(ent->sym, sym, MAX_SYM_SIZE);
    ent->sym[MAX_SYM_SIZE - 1] = '\0'; //For extra safety
    memcpy(ent->dat, dat, dat_len);
    ent->dat_len = dat_len;
    
    s->error_str = SYMTAB_SUCC;
    return 0;
}

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets s->error_str). By the way, also makes sure that no duplicate syms
//are added, although will update data if the sym is found in the array
//NOTE: returns -2 if s was NULL. 
//This function is slow as hell, but who cares?
int symtab_append_nodup(symtab *s, char *sym, void *dat, int dat_len) {
    //Sanity check on input
    if (s == NULL) {
        return -2; //This is all we can do
    }
    
    if (sym == NULL) {
        s->error_str = SYMTAB_NULLARG;
        return -1;
    }
    
    if (dat_len < 0) {
        s->error_str = SYMTAB_INVALID;
        return -1;
    }
    
    if (dat_len > MAX_SYM_DATA) {
        s->error_str = SYMTAB_TOO_BIG;
        return -1;
    }
    
    //First, check if this sym is already in the table
    int i;
    for (i = 0; i < s->nents; i++) {
        if (strcmp(s->entries[i].sym, sym) == 0) {
            memcpy(s->entries[i].dat, dat, dat_len);
            s->entries[i].dat_len = dat_len;
            return 0;
        }
    }
    
    //If we got here, it's because sym is not in the list
    int rc = symtab_append(s, sym, dat, dat_len);
    if (rc < 0) {
        return -1; //symtab_array_append already set error_str
    }
    
    return 0;
}

//Simple deletion by overwriting the element at ind with a copy of the last
//element in the array, then decreasing the array's size. Returns 0 on 
//success, -1 on error (and sets s->error_str appropriately). NOTE: returns
//-2 if s was NULL
int symtab_remove_at_index(symtab *s, int ind) {
    //Sanity check inputs
    if (s == NULL) {
        return -2; //This is all we can do
    }
    if (ind < 0 || ind >= s->nents) {
        s->error_str = SYMTAB_OOB;
        return -1;
    }
    
    //Perform the deletion
    s->nents--;
    s->entries[ind] =  s->entries[s->nents]; //More expensive than necessary,
    //but who cares?
    
    s->error_str = SYMTAB_SUCC;
    return 0;
}

//Tries to remove the symtab_entry pointed to by ent from the array. 
//Returns 0 on success, -1 on error (and sets s->error_str if possible).
//NOTE: returns -2 if s is NULL
int symtab_array_remove(symtab *s, symtab_entry *ent) {
    //Sanity check inputs
    if (s == NULL) {
        return -2; //This is all we can do
    }
    
    if (ent == NULL) {
        s->error_str = SYMTAB_NOT_MEMB;
        return -1;
    }
    
    //Get index of given pollfd struct
    int ind = ent - s->entries; //Remember C's rules abot pointer arithmetic
    if (ind < 0 || ind >= s->nents) {
        s->error_str = SYMTAB_NOT_MEMB;
        return -1;
    }
    
    return symtab_remove_at_index(s, ind);
}

//Looks up the entry under sym. Returns a pointer to the symtab_entry struct
//on success (see the sym_dat macro) or NULL on error, and sets 
//s->error_str (if possible)
//Slow as hell, but who cares?
symtab_entry* symtab_lookup(symtab *s, char *sym) {
    //Sanity check inputs
    if (s == NULL) {
        return NULL; //This is all we can do
    }
    
    if (sym == NULL) {
        s->error_str = SYMTAB_NULLARG;
        return NULL;
    }
    
    int i;
    for (i = 0; i < s->nents; i++) {
        if (strcmp(s->entries[i].sym, sym) == 0) {
            s->error_str = SYMTAB_SUCC;
            return s->entries + i;
        }
    }
    
    //Not found!
    s->error_str = SYMTAB_NOT_FOUND;
    return NULL;
}
