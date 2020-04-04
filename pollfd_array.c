#ifdef DEBUG_ON
#include <stdio.h>
#include <string.h>
#endif

#include <poll.h>
#include <stdlib.h>
#include "pollfd_array.h"

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////
char const *const PFD_ARR_SUCC = "success";
char const *const PFD_ARR_OOM = "out of memory";
char const *const PFD_ARR_OOB = "out of bounds";
char const *const PFD_ARR_NOT_MEMB = "given pollfd struct is not in the array";
char const *const PFD_ARR_NULL_ARG = "non-NULL argument expected";


//Returns a malloc'ed pollfd_array, or NULL on error
pollfd_array* new_pollfd_array(int howmany) {
    //Sanity check on input
    if (howmany < 0) return NULL;
    
    pollfd_array *ret = malloc(sizeof(pollfd_array));
    if (!ret) return NULL;
    
    ret->pfds = malloc(howmany * sizeof(struct pollfd));
    if (ret->pfds == NULL) {
        free(ret);
        return NULL;
    }
    
    ret->user_data = malloc(howmany * sizeof(void *));
    if (ret->user_data == NULL) {
        free(ret->pfds);
        free(ret);
        return NULL;
    }
    
    ret->num = 0;
    ret->cap = howmany;
    
    ret->error_str = PFD_ARR_SUCC;
    return ret;
}

//Frees memory allocated in *p. Gracefully ignores NULL input
void del_pollfd_array(pollfd_array *p) {
    if (!p) return;
    
    if (p->pfds != NULL) free(p->pfds);
    if (p->user_data != NULL) free(p->user_data);
    
    free(p);
}

//Returns 0 on success, else -1 and sets p->error_str
static int make_space_for_one_more(pollfd_array *p) {
    if (p->num < p->cap) return 0;
    
    struct pollfd *res = realloc(p->pfds, p->cap * 2 * sizeof(struct pollfd));
    
    if (res == NULL) {
        p->error_str = PFD_ARR_OOM;
        return -1;
    } else {
        p->pfds = res;
    }
    
    void **res_user_data = realloc(p->user_data, p->cap * 2 * sizeof(void *));
    if (res == NULL) {
        //The pollfd_array struct is still usable. The next time we try to
        //realloc the pollfd struct array, it will just trivially succeed
        //since it is already the desired size.
        p->error_str = PFD_ARR_OOM;
        return -1;
    } else {
        p->user_data = res_user_data;
    }
    
    p->cap *= 2;
    return 0;
}

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets p->error_str). 
//NOTE: returns -2 if p was NULL. 
//This function is slow as hell, but who cares?
int pollfd_array_append(pollfd_array *p, int fd, unsigned events, void *user_data) {
    //Sanity check on input
    if (p == NULL) {
        return -2; //This is all we can do
    }
    
    //If we got here, it's because fd is not in the list
    int rc = make_space_for_one_more(p);
    if (rc < 0) {
        return -1; //make_space_for_one_more already set error_str
    }
    
    struct pollfd *pfd = p->pfds + p->num;
    pfd->fd = fd;
    pfd->events = events;
    
    p->user_data[p->num++] = user_data;
    
    #ifdef DEBUG_ON
    fprintf(stderr, "Array now has %d elements\n", p->num);
    fflush(stderr);
    #endif
    
    return 0;
}

//Adds new entry to the end of the array. Returns 0 on success, -1 on error 
//(and sets p->error_str). By the way, also makes sure that no duplicate fds
//are added, although will update events if the fd is found in the array
//NOTE: returns -2 if p was NULL. 
//This function is slow as hell, but who cares?
int pollfd_array_append_nodup(pollfd_array *p, int fd, unsigned events, void *user_data) {
    //Sanity check on input
    if (p == NULL) {
        return -2; //This is all we can do
    }
    
    //First, check if this fd is already in the array
    int i;
    for (i = 0; i < p->num; i++) {
        if (p->pfds[i].fd == fd) {
            p->pfds[i].events = events;
            p->user_data[i] = user_data;
            return 0;
        }
    }
    
    //If we got here, it's because fd is not in the list
    int rc = pollfd_array_append(p, fd, events, user_data);
    if (rc < 0) {
        return -1; //pollfd_array_append already set error_str
    }
    
    return 0;
}

//Simple deletion by overwriting the element at ind with a copy of the last
//element in the array, then decreasing the array's size. Returns 0 on 
//success, -1 on error (and sets p->error_str appropriately). NOTE: returns
//-2 if p was NULL
int pollfd_array_remove_at_index(pollfd_array *p, int ind) {
    //Sanity check inputs
    if (p == NULL) {
        return -2; //This is all we can do
    }
    if (ind < 0 || ind >= p->num) {
        p->error_str = PFD_ARR_OOB;
        return -1;
    }
    
    //Perform the deletion
    p->num--;
    p->pfds[ind] = p->pfds[p->num];
    p->user_data[ind] = p->user_data[p->num];
    
    p->error_str = PFD_ARR_SUCC;
    return 0;
}

//Tries to remove the pollfd struct pointed to by pfd from the array. 
//Returns 0 on success, -1 on error (and sets p->error_str if possible).
//NOTE: returns -2 if p is NULL
int pollfd_array_remove(pollfd_array *p, struct pollfd *pfd) {
    //Sanity check inputs
    if (p == NULL) {
        return -2; //This is all we can do
    }
    
    if (pfd == NULL) {
        p->error_str = PFD_ARR_NULL_ARG;
        return -1;
    }
    
    //Get index of given pollfd struct
    int ind = pfd - p->pfds; //Remember C's rules abot pointer arithmetic
    if (ind < 0 || ind >= p->num) {
        p->error_str = PFD_ARR_NOT_MEMB;
        return -1;
    }
    
    return pollfd_array_remove_at_index(p, ind);
}

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
struct pollfd* pollfd_array_get_active(pollfd_array *p, struct pollfd *start) {
    //Sanity check inputs
    if (p == NULL) {
        return NULL; //This is all we can do
    }
    
    //Nothing to traverse.
    if (p->num == 0) {
        p->error_str = PFD_ARR_SUCC;
        return NULL;
    }
    
    //Calculate where to start reading the array
    int start_ind;
    if (start == NULL) {
        start_ind = 0;
    } else {    
        //Get index of given pollfd struct
        start_ind = start - p->pfds; //Remember C's rules about pointer arithmetic
        if (start_ind < 0 || start_ind >= p->num) {
            p->error_str = PFD_ARR_NOT_MEMB;
            return NULL;
        }
        start_ind++; //Don't recheck this member of the array
    }
    
    int i;
    for (i = start_ind; i < p->num; i++) {
        if (p->pfds[i].revents != 0) {
            p->error_str = PFD_ARR_SUCC;
            return p->pfds + i;
        }
    }
    
    //If we got here, it's because nothing was found. Return NULL and success
    p->error_str = PFD_ARR_SUCC;
    return NULL;
}

//Gets a pointer to the user_data that was inserted alongside the fd in pfd.
//You can use this pointer to change the user_data, if you want. 
//Returns a non-NULL pointer on success, NULL on error. On error, sets 
//p->error_str (if available)
void* pollfd_array_get_user_data(pollfd_array *p, struct pollfd *pfd) {
    //Sanity check inputs
    if (p == NULL) return NULL;
    
    if (pfd == NULL) {
        p->error_str = PFD_ARR_NULL_ARG;
        return NULL;
    }
    
    //Get index of given pollfd struct
    int ind = pfd - p->pfds; //Remember C's rules abot pointer arithmetic
    if (ind < 0 || ind >= p->num) {
        p->error_str = PFD_ARR_NOT_MEMB;
        return NULL;
    }
    
    return p->user_data + ind;
}
