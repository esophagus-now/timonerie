#ifndef POLLFD_ARRAY_H
#define POLLFD_ARRAY_H 1

#include <poll.h>

//This is bunch of helper functions for manipulating an array of pollfds.
//Because I don't expect these functions to be called more than a handful
//of times over the course of the whole program, they are not at all
//optimized.

typedef struct _pollfd_array {
    struct pollfd *pfds; //Array of struct pollfds
    void **user_data;    //For each pollfd, the user can save their own pointer
    int num; //How many array entries are used
    int cap; //How much total space is there
    
    //Error information
    char const *error_str;
} pollfd_array;

//Returns a malloc'ed pollfd_array, or NULL on error
pollfd_array* new_pollfd_array(int howmany);

//Frees memory allocated in *p. Gracefully ignores NULL input
void del_pollfd_array(pollfd_array *p);

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets p->error_str). 
//NOTE: returns -2 if p was NULL. 
//This function is slow as hell, but who cares?
int pollfd_array_append(pollfd_array *p, int fd, unsigned events, void *user_data);

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets p->error_str). By the way, also makes sure that no duplicate fds
//are added, although will update events if the fd is found in the array
//NOTE: returns -2 if p was NULL. 
//This function is slow as hell, but who cares?
int pollfd_array_append_nodup(pollfd_array *p, int fd, unsigned events, void *user_data);

//Simple deletion by overwriting the element at ind with a copy of the last
//element in the array, then decreasing the array's size. Returns 0 on 
//success, -1 on error (and sets p->error_str appropriately). NOTE: returns
//-2 if p was NULL
int pollfd_array_remove_at_index(pollfd_array *p, int ind);

//Tries to remove the pollfd struct pointed to by pfd from the array. 
//Returns 0 on success, -1 on error (and sets p->error_str if possible).
//NOTE: returns -2 if p is NULL
int pollfd_array_remove(pollfd_array *p, struct pollfd *pfd);

//Use this function to traverse active fds. If the second argument is NULL,
//starts at the beginning of the array in p. Otherwise, starts at the array 
//member given as the second argument. Returns NULL on end of traversal. If
//an error occurred, returns NULL, and also sets p->error_str (if the given
//p is non-NULL). Intended use:
//
//  pollfd_array *p;
//  ....
//  struct pollfd *cur = NULL;
//  while (cur = pollfd_array_get_active(p, cur)) {
//    ...
//  }
//
//  if (p->error_str != PFD_ARR_SUCC) {
//    //Check error...
//  }
struct pollfd* pollfd_array_get_active(pollfd_array *p, struct pollfd *start);

//Gets a pointer to the user_data that was inserted alongside the fd in pfd.
//You can use this pointer to change the user_data, if you want. 
//Returns a non-NULL pointer on success, NULL on error. On error, sets 
//p->error_str (if available)
void* pollfd_array_get_user_data(pollfd_array *p, struct pollfd *pfd);

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
extern char const *const PFD_ARR_SUCC; //= "success";
extern char const *const PFD_ARR_OOM;  //= "out of memory";
extern char const *const PFD_ARR_OOB;  //= "out of bounds";
extern char const *const PFD_ARR_NOT_MEMB;  //= "given pollfd struct is not in the array";
extern char const *const PFD_ARR_NULL_ARG;  //= "non-NULL argument expected";


#endif

