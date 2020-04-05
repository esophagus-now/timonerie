#ifndef TWM_H
#define TWM_H 1

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

extern char const *const TWM_SUCC; // = "success";
extern char const *const TWM_LEAF_SZ_ERR; // = "TWM_LEAF node size check failed";
extern char const *const TWM_LEAF_DRAW_ERR; // = "TWM_LEAF node draw failed";
extern char const *const TWM_LEAF_TRIG_REDRAW_ERR; // = "TWM_LEAF node redraw trigger failed";
extern char const *const TWM_BAD_NODE_TYPE; // = "invalid node type (uninitialized memory?)";
extern char const *const TWM_BAD_SZ; // = "bad node size";
extern char const *const TWM_BAD_POS; // = "bad node position";
extern char const *const TWM_FULL_PARENT; // = "too many children";
extern char const *const TWM_NULL_DRAW_FN; // = "window has no draw function";
extern char const *const TWM_NULL_DRAW_SZ; // = "window has no size-finding function";
extern char const *const TWM_NULL_TRIG_REDRAW; // = "window has no redraw trigger function";
extern char const *const TWM_NO_WINDOWS; // = "no windows in tree";
extern char const *const TWM_INVALID_TREE; // = "window tree is in a broken state!";
extern char const *const TWM_NOT_FOUND; // = "could not find child in list";
extern char const *const TWM_IMPOSSIBLE; // = "TWM code reached a place Marco thought was impossible";
extern char const *const TWM_NULL_ARG; // = "NULL argument where non-NULL was expected";
extern char const *const TWM_BAD_DIR; // = "bad direction";
extern char const *const TWM_OOM; // = "out of memory";
extern char const *const TWM_OOB; // = "out of bounds";
extern char const *const TWM_ILLEGAL_DELETE; // = "illegal node deletion";

//Draws item. Returns number of bytes added into buf, or -1 on error.
typedef int draw_fn_t(void *item, int x, int y, int w, int h, char *buf);

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
typedef int draw_sz_t(void *item, int w, int h);

//Tells item that it should redraw, probably because it moved to another
//area of the screen
typedef void trigger_redraw_t(void *item);

//If a drawable does not want to implement this, they can leave the pointer
//as NULL inside draw_operations
typedef void set_error_str_t(void *item, char const *str);

//All drawable items must implement this interface
typedef struct _draw_operations {
    draw_fn_t *draw_fn;
    draw_sz_t *draw_sz;
    trigger_redraw_t *trigger_redraw;
    set_error_str_t *set_error_str; //TODO: probably remove, but make sure
} draw_operations;

typedef enum _twm_node_type {
    TWM_LEAF,
    TWM_HORZ,
    TWM_VERT
} twm_node_type;

//These should be opaque structs
#define MAX_CHILDREN 8
typedef struct _twm_node {
    //If this is a TWM_LEAF node, then draw the item pointed to by item_to_draw
    //using the function pointed at by how_to_draw_it. Otherwise, we need 
    //to recurse and draw each of the children
    twm_node_type type;
    struct _twm_node *parent;
    
    //This means that this node's borders will be redrawn. Has no bearing 
    //on children, and is unused when type == TWM_LEAF
    int need_redraw;
    
    //This node and all its children will be highlighted
    int has_focus;
    
    //If this is a TWM_LEAF, simply draw the item. The pointer and function
    //pointers contain all the necessary instructions to do it.
    void *item;
    draw_operations draw_ops;
    
    //Otherwise, this node either contains TWM_VERTically or horizontally 
    //stacked "windows"
    struct _twm_node *children[MAX_CHILDREN];
    int num_children;
    
    //Error information
    char const *error_str;
} twm_node;

//Invariant maintained by the functions in this library: no non-leaf node 
//has one or zero children. Also, I guess another invariant is that all 
//the pointers and number of children are in a valid range.
typedef struct _twm_tree {
    twm_node *head;
    twm_node *focus; //Points to the window that the user is "focused" on
                     //This is used for positioning new windows, and if I
                     //later decide to draw borders differently around the
                     //focused window
    
    //Error informaiton
    char const *error_str;
} twm_tree;

#define TOO_FEW_CHILDREN 1
#define TREE_CORRUPTED 2

//Returns a bitmap of all failed invariants
static int check_tree_invariants_internal(twm_node *t) {
    if (t == NULL) {
        //An empty tree is considered valid
        return 0;
    }
    
    if (t->type == TWM_LEAF) {
        return 0;
    } else {
        int rc = 0;
        if (t->num_children < 2) {
            rc |= TOO_FEW_CHILDREN;
        }
        
        if (t->num_children < 0 || t->num_children >= MAX_CHILDREN) {
            rc |= TREE_CORRUPTED;
            return rc;
        }
        
        int i;
        for (i = 0; i < t->num_children; i++) {
            twm_node *child = t->children[i];
            if (child == NULL) {
                rc |= TREE_CORRUPTED;
                continue;
            }
            
            rc |= check_tree_invariants(child);
        }
        
        return rc;
    }
    
    return TREE_CORRUPTED;
}

//Here's the key idea: twm nodes also know how to draw themselves
int draw_fn_twm_node(void *item, int x, int y, int w, int h, char *buf) {
    twm_node *t = (twm_node *)item;
    
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (w < 0 || h < 0) {
        t->error_str = TWM_BAD_SZ;
        return -1;
    }
    
    if (x < 0 || y < 0) {
        t->error_str = TWM_BAD_POS;
        return -1;
    }
    
    //Draw this node
    char *buf_saved = buf; //Keep track of original position so we can calculate size
    if (t->has_focus) {
        *buf++ = '\e'; *buf++ = '['; *buf++ = '1'; *buf++ = 'm'; //Turn on highlight mode
    }
    if (t->type == TWM_LEAF) {
        //Check if a proper draw fn is given
        if (t->draw_ops.draw_fn == NULL) {
            t->error_str = TWM_NULL_DRAW_FN;
            return -1;
        }
        //Return bytes consumed by the TWM_LEAF node
        int child_sz = t->draw_ops.draw_fn(t->item, x, y, w, h, buf);
        if (child_sz < 0) {
            t->error_str = TWM_LEAF_DRAW_ERR;
        }
        buf += child_sz;
        
        if (t->has_focus) {
            *buf++ = '\e'; *buf++ = '['; *buf++ = 'm'; //Restore original mode
        }
        return buf_saved - buf;
    } else if (t->type == TWM_HORZ) {        
        //In a perfect world, the width (minus the columns needed for drawing
        //separating borders) is an exact multiple of the number of children.
        //However, here we're forced to keep track of how much error we have
        int N = w - (t->num_children - 1);
        int D = t->num_children;
        int quot = N / D;
        int rem = N % D;
        
        //This is how many "fractional columns" are available.
        int err = rem;
        
        //Keep track of where we're drawing the children
        int child_x = x;
        
        //Draw children.        
        int i;
        for (i = 0; i < t->num_children; i++) {
            int child_width = quot;
            if (err >= D) {
                child_width++;
                err -= D;
            }
            int child_sz = draw_fn_twm_node(t->children[i], child_x, y, child_width, h, buf);
            if (child_sz < 0) {
                //Propagate error upwards
                t->error_str = t->children[i]->error_str;
                return child_sz;
            }
            buf += child_sz;
            err += rem;
            child_x += child_width; //Advance past end of child we just drew
            
            //Now, if this node is set to redraw, draw borders between this 
            //child and the next, provided we're not on the last child
            if (t->need_redraw && i != t->num_children - 1) {
                int incr = cursor_pos_cmd(buf, child_x, y);
                buf += incr;
                *buf++ = '|';
                
                int j;
                for (j = 1; j < h; j++) {
                    *buf++ = '\e'; *buf++ = '['; *buf++ = 'B'; //Move cursor down
                    *buf++ = '\e'; *buf++ = '['; *buf++ = 'D'; //Move cursor to the left
                    *buf++ = '|'; //Draw border line
                }
            }
            child_x++; //Advance past border line
        }
        
        if (t->has_focus) {
            *buf++ = '\e'; *buf++ = '['; *buf++ = 'm'; //Restore original mode
        }
        
        t->need_redraw = 0;
        
        t->error_str = TWM_SUCC;
        return buf - buf_saved;
    } else if (t->type == TWM_VERT) {        
        //In a perfect world, the width (minus the columns needed for drawing
        //separating borders) is an exact multiple of the number of children.
        //However, here we're forced to keep track of how much error we have
        int N = h - (t->num_children - 1);
        int D = t->num_children;
        int quot = N / D;
        int rem = N % D;
        
        //This is how many "fractional columns" are available.
        int err = rem;
        
        //Keep track of where we're drawing the children
        int child_y = y;
        
        //Draw children.        
        int i;
        for (i = 0; i < t->num_children; i++) {
            int child_height = quot;
            if (err >= D) {
                child_width++;
                err -= D;
            }
            int child_sz = draw_fn_twm_node(t->children[i], x, child_y, w, child_height, buf);
            if (child_sz < 0) {
                //Propagate error upwards
                t->error_str = t->children[i]->error_str;
                return child_sz;
            }
            buf += child_sz;
            
            err += rem;
            
            child_y += child_height; //Advance past end of child we just drew
            
            //Now, if this node is set to redraw, draw borders between this 
            //child and the next, provided we're not on the last child
            if (t->need_redraw && i != t->num_children - 1) {
                int incr = cursor_pos_cmd(buf, x, child_y);
                buf += incr;
                
                int j;
                for (j = 0; j < w; j++) {
                    *buf++ = '-'; //Draw border line
                }
            }
            child_y++; //Advance past border line
        }
        
        if (t->has_focus) {
            *buf++ = '\e'; *buf++ = '['; *buf++ = 'm'; //Restore original mode
        }
        
        t->need_redraw = 0;
        
        t->error_str = TWM_SUCC;
        return buf - buf_saved;
    }
}

int draw_sz_twm_node(void *item, int w, int h) {
    twm_node *t = (twm_node *)item;
    
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (w < 0 || h < 0) {
        t->error_str = TWM_BAD_SZ;
        return -1;
    }
    
    int total_sz = 0;
    if (t->has_focus) {
        total_sz += 7; //Extra characters needed to turn on highlight and
                       //ten turn it off afterwards
    }
        
    //Calculate size
    if (t->type == TWM_LEAF) {
        //Check if a valid size function was given
        if (t->draw_ops.draw_sz == NULL) {
            t->error_str = TWM_NULL_DRAW_SZ;
            return -1;
        }
        //Simply return the amount of space needed by the contained drawable
        int child_sz = t->draw_ops.draw_sz(t->item, w, h);
        if (child_sz < 0) {
            t->error_str = TWM_LEAF_SZ_ERR;
        }
        return child_sz;
    } else if (t->type == TWM_HORZ) {
        
        //In a perfect world, the width (minus the columns needed for drawing
        //separating borders) is an exact multiple of the number of children.
        //However, here we're forced to keep track of how much error we have
        int N = w - (t->num_children - 1);
        int D = t->num_children;
        int quot = N / D;
        int rem = N % D;
        
        //This is how many "fractional columns" are available.
        int err = rem;
        
        //Tally up size needed by all the children
        int i;
        for (i = 0; i < t->num_children; i++) {
            int child_width = quot;
            if (err >= D) {
                child_width++;
                err -= D;
            }
            int child_sz = draw_sz_twm_node(t->children[i], child_width, h);
            if (child_sz < 0) {
                //Propagate error upwards
                t->error_str = t->children[i]->error_str;
                return child_sz;
            }
            total_sz += child_sz;
            err += rem;
        }
        
        //Now add in size we need for drawing the borders, provided this node needs a redraw
        int border_sz = 0;   //Size needed for a single border line
        if (t->need_redraw) {
            border_sz += 10; //Upper bound of bytes needed to place the cursor at the top of the line
            border_sz += 1;  //The first '|' character of the border
            border_sz += 7*(h-1); //Each succeeding '|' requires 6 bytes to move the cursor to the right place, then one more for the '|' character
        }
        
        total_sz += (t->num_children - 1) * border_sz;
        
        t->error_str = TWM_SUCC;
        return total_sz;
    } else if (t->type == TWM_VERT) {        
        //In a perfect world, the height (minus the rows needed for drawing
        //separating borders) is an exact multiple of the number of children.
        //However, here we're forced to keep track of how much error we have
        int N = h - (t->num_children - 1);
        int D = t->num_children;
        int quot = N / D;
        int rem = N % D;
        
        //This is how many "fractional columns" are available.
        int err = rem;
        
        //Tally up size needed by all the children
        int i;
        for (i = 0; i < t->num_children; i++) {
            int child_height = quot;
            if (err >= D) {
                child_height++;
                err -= D;
            }
            int child_sz = draw_sz_twm_node(t->children[i], w, child_height);
            if (child_sz < 0) {
                //Propagate error upwards
                t->error_str = t->children[i]->error_str;
                return child_sz;
            }
            err += rem;
        }
        
        //Now add in size we need for drawing the borders, provided we need
        //to redraw this node
        
        int border_sz = 0;   //Size needed for a single border line
        if (t->need_redraw) {
            border_sz += 10; //Upper bound of bytes needed to place the cursor at the left of the line
            border_sz += w;  //Number of '-' characters on this line
        }
        
        total_sz += (t->num_children - 1) * border_sz;
        
        t->error_str = TWM_SUCC;
        return total_sz;
    }
    
    //Code should never get here
    t->error_str = TWM_BAD_NODE_TYPE;
    return -1;
}

void trigger_redraw_twm_node(void *item) {
    twm_node *t = (twm_node *)item;
    if (t != NULL) t->need_redraw = 1;
}

//Special functions for empty nodes
int draw_fn_empty(void *item, int x, int y, int w, int h, char *buf) {return 0;}
int draw_sz_empty(void *item, int w, int h) {return 0;}
void trigger_redraw_empty(void *item) {return;}

draw_operations const empty_ops = {
    .draw_fn = draw_fn_empty,
    .draw_sz = draw_sz_empty,
    .trigger_redraw = trigger_redraw_empty
};

//Returns a newly allocated twm_node with given drawable, or NULL on error
static twm_node* construct_leaf_twm_node(void *item, draw_operations draw_ops) {
    twm_node *ret = calloc(1, sizeof(twm_node));
    if (!ret) return NULL;
    
    ret->type = TWM_LEAF;
    ret->item = item;
    ret->draw_ops = draw_ops;
    
    ret->error_str = TWM_SUCC;
    return ret;
}

//Free twm_node. Gracefully ignores NULL
static void destroy_twm_node(twm_node *t) {
    if (t == NULL) return;
    
    free(t);
}

//Free twm_node tree rooted at this node. Gracefully ignores NULL input
static void free_twm_node_tree(twm_node *head) {
    if (head == NULL) return;
    
    if (head->type == TWM_LEAF) {
        free(head);
        return;
    } else {
        int i;
        for (i = 0; i < head->num_children; i++) {
            free_twm_node_tree(head->children[i]);
        }
        free(head);
        return;
    }
}

//Mark all nodes in the subtree for redraw. Returns -1 on error (and sets
//t->error_str), or -2 if t was NULL
static int redraw_twm_node_tree(twm_node *t) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (t->type == TWM_LEAF) {
        //Check if a trigger redraw function was given
        if (t->draw_ops.trigger_redraw == NULL) {
            t->error_str = TWM_NULL_TRIG_REDRAW;
            return -1;
        }
        
        //Trigger redraw, and cehck for errors
        int rc = t->draw_ops.trigger_redraw(t->item);
        if (rc < 0) {
            t->error_str = TWM_LEAF_TRIG_REDRAW_ERR;
            return -1;
        }
        
        //Success
        t->error_str = TWM_SUCC;
        return 0;
    } else {
        //Make sure we actually have children
        if (t->num_children == 0) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        int i;
        for (i = 0; i < t->num_children; i++) {
            twm_node *cur = t->children[i];
            
            //Check if this is at least non-NULL
            if (cur == NULL) {
                t->error_str = TWM_INVALID_TREE;
                return -1;
            }
            
            //Recursively trigger redraw
            int rc = redraw_twm_node_tree(cur);
            if (rc < 0) {
                //Propagate error up
                t->error_str = cur->error_str;
                return -1;
            }
            
            //Success
            t->error_str = TWM_SUCC;
            return 0;
        }
    }
    
    t->error_str = TWM_IMPOSSIBLE;
    return -1;
}

//Returns a new twm_tree on success, NULL on error
twm_tree* new_twm_tree() {
    twm_tree *ret = calloc(1, sizeof(twm_tree));
    
    ret->error_str = TWM_SUCC;
    return ret;
}

//Frees twm_tree and all associated resources. Gracefully ignores NULL input
void del_twm_tree(twm_tree *t) {
    if (t == NULL) return;
    
    free_twm_node_tree(t->head);
    
    free(t);
}

//Inserts a new drawable, based on t's currently focused window. Returns 0
//on success, or -1 on error (and sets t->error_str). Returns -2 if t was
//NULL
int twm_tree_add_window(twm_tree *t, void *item, draw_operations draw_ops) {
    
}

//Directions for moving focus/windows
typedef enum _twm_dir {
    TWM_UP,
    TWM_DOWN,
    TWM_LEFT,
    TWM_RIGHT,
    TWM_PARENT,
    TWM_CHILD
} twm_dir;

//Linear search for t inside p->children. Returns index on success, -1 oon
//error (and sets p->error_str). Returns -2 if t was NULL
static int twm_node_indexof(twm_node *t, twm_node *p) {
    //Check for NULL inputs
    if (p == NULL) {
        return -2; //This is all we can do
    }
    
    if (t == NULL) {
        p->error_str = TWM_NULL_ARG;
        return -1;
    }
    
    //Make sure t has children
    if (p->num_children == 0) {
        p->error_str = TWM_INVALID_TREE;
        return -1;
    }
    
    //Perform linear search
    int i;
    for (i = 0; i < p->num_children; i++) {
        if (p->children[i] == t) {
            p->error_str = TWM_SUCC;
            return i;
        }
    }
    
    //If we got here, t was not found
    p->error_str = TWM_NOT_FOUND;
    return -1;
}

//Returns 0 on success, -1 on error (and sets t->error_str). Returns -2 if
//t is NULL.
int twm_tree_move_focus(twm_tree *t, twm_dir dir) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (t->focus == NULL) {
        //Check for corner case
        if (t->head == NULL) {
            t->error_str = TWM_NO_WINDOWS;
            return -1;
        }
        
        //Not currently focused on anything. Pick the "leftmost" child if one
        //is available
        twm_node *cur = t->head;
        while (cur->type != TWM_LEAF) {
            //Make sure this node actually has children
            if (t->focus->num_children == 0) {
                t->error_str = TWM_INVALID_TREE;
                return -1;
            }
            
            //Select first child in the list
            cur = t->focus->children[0];
            if (cur == NULL) {
                t->error_str = TWM_INVALID_TREE;
                return -1;
            }
        }
        
        t->focus = cur;
        t->focus->has_focus = 1;
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error code
            t->error_str = t->focus->error_str;
            return -1;
        }
        return 0;
    }
    
    //If we are focused on the root of the tree, we can't move the focus
    if (t->focus->parent == NULL) {
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //The TWM_PARENT and TWM_CHILD directions are easy to implement
    if (dir == TWM_PARENT) {
        t->focus->has_focus = 0;
        //Note: don't need to redraw this subtree because the parent's
        //subtree includes it
        
        t->focus = t->focus->parent;
        
        t->focus->has_focus = 1;
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error upwards
            t->error_str = t->focus->error_str;
            return -1;
        }
    } else if (dir == TWM_CHILD) {
        if (t->focus->type == TWM_LEAF) {
            //Nothing to do
            t->error_str = TWM_SUCC;
            return 0;
        }
        
        //Make sure this node actually has children
        if (t->focus->num_children == 0) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        //Select first child in the list
        twm_node *cur = t->focus->children[0];
        if (cur == NULL) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        t->focus->has_focus = 0;
        cur->has_focus = 1;
        
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error upwards
            t->error_str = t->focus->error_str;
            t->focus = NULL; //For safety?
            return -1;
        }
        t->focus = cur;
        //No need to redraw this tree since it's a subtree of the last one
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //Now the tricky business. Following "sane" rules, move focus to where
    //the user intends
    twm_node *cur = t->focus->parent;
    twm_node *prev = t->focus;
    
    int list_dir = (dir == UP || dir == LEFT) ? -1 : 1;
    
    while (cur != NULL) {
        //If we are going TWM_UP or TWM_DOWN, and cur is a TWM_HORZ node,
        //then we have to go up the heirarchy. Likewise for TWM_LEFT and
        //TWM_RIGHT in a TWM_VERT node.
        if (((dir == TWM_UP || dir == TWM_DOWN) && cur->type == TWM_HORZ) ||
            ((dir == TWM_LEFT || dir == TWM_RIGHT) && cur->type == TWM_VERT)) 
        {
            //Keep going up the heirarchy
            prev = cur;
            cur = cur->parent;
            continue;
        }

        //Otherwise, try moving along the list of children
        
        //First find index of prev in cur'S child list
        int ind = twm_node_indexof(prev, cur);
        if (ind < 0) {
            //Propagate error
            t->error_str = cur->error_str;
            return -1;
        }
        
        //If we are already at the end, we need to go up the hierarchy
        if (ind + list_dir < 0 || ind + list_dir >= cur->num_children) {
            //Keep going up the heirarchy
            prev = cur;
            cur = cur->parent;
            continue;
        }
        
        //Finally! We have a case we can deal with. Focus on the predecessor 
        //of prev in the child list
        cur = cur->children[ind + list_dir];
        
        //Make sure cur is non-NULL
        if (cur == NULL) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        //Set focus and redraw
        t->focus->has_focus = 0;
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error upward
            t->error_str = t->focus->error_str;
            return -1;
        }
        
        cur->focus = 1;
        rc = redraw_twm_node_tree(cur);
        if (rc < 0) {
            //Propagate error upward
            t->error_str =cur->error_str;
            return -1;
        }
        
        t->focus = cur;
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //There is no good place to move the focus, so just do nothing
    t->error_str = TWM_SUCC;
    return 0;
}

//Follows usual error-return technique. Also makes sure to set redraws
//where necessary
static int remove_node(twm_tree *t, twm_node *src) {
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (src == NULL) {
        t->error_str = TWM_NULL_ARG;
        return -1;
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "remove_node precondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    //Actually remove the node
    
    //Get a reference to the parent of the node to remove
    twm_node *parent = src->parent;
    
    //Special case: we can only delete the head of the tree if it is a leaf
    //node
    if (parent == NULL) {
        if (src->type != LEAF) {
            t->error_str = TWM_ILLEGAL_DELETE;
            return -1;
        }
        
        t->head = NULL;
        t->focus = NULL;
        
        destroy_twm_node(src);
        
        #ifdef DEBUG_ON
        fprintf(stderr, "remove_node postcondition: 0x%x\n", check_tree_invariants(t->head));
        fflush(stderr);
        #endif
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //Now the ugly case. First find the index of this node inside its parent's
    //list of children
    int ind = twm_node_indexof(src, parent);
    if (ind < 0) {
        //Propagate error
        t->error_str = parent->error_str;
        return -1;
    }
    
    //Remove the node from the list it is in
    int i;
    for (i = ind; i < parent->num_children - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->num_children--;
    
    //Edit the parent to make sure we don't break any imvariants. 
    if (parent->num_children == 1) {
        twm_node *tmp = parent->children[0];
        parent->type = tmp->type;
        parent->item = tmp->item;
        parent->draw_ops = tmp->draw_ops;
        parent->num_children = tmp->num_children;
        for (i = 0; i < cur->num_children; i++) {
            parent->children[i] = tmp->children[i];
        }
        destroy_twm_node(tmp);
        
        //Make sure we redraw this node and all its children, which have
        //almost certainly all moved
        redraw_twm_node_tree(parent);
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "remove_node postcondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    t->error_str = TWM_SUCC;
    return 0;
}

//Inserts tje twm_node (specified as the node pointed to by src) into the
//location (specified as the dst_ind'th child of dst). Follows usual error
//return method
static int insert_node(twm_tree *t, twm_node *src, twm_node *dst, int dst_ind) {
    //Sanity check inputs
    if (t == NULL) {
        return -2;
    }
    if (src == NULL || dst == NULL) {
        t->error_str = TWM_NULL_ARG;
        return -1;
    }
    if (dst_ind < 0 || dst_ind > dst->num_children) {
        t->error_str = TWM_BAD_POS;
        return -1;
    }
    
    //Special case: if there is only one node in the tree, then it is a leaf
    //and we need to convert it to a stacked node
    
}

//Moves a twm_node (specified as the node pointed to by src) to another
//location (specified as the dst_ind'th child of dst). Follows usual error
//return method
static int move_node(twm_tree *t, twm_node *src, twm_node *dst, int dst_ind) {
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "move_node precondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    if (src == NULL || dst == NULL) {
        t->error_str = TWM_NULL_ARG;
        return -1;
    }
    if (src_ind < 0 || src_ind >= src->num_children) {
        t->error_str = TWM_OOB;
        return -1;
    }
    if (dst_ind < 0 || dst_ind >= dst->num_children) {
        t->error_str = TWM_OOB;
        return -1;
    }
    
    //First, begin by deleting src->children[src_ind] from the tree. As we
    //do that, we have to be careful to update dst and dst_ind if their 
    //part of the tree is modified by the deletion
    
    //Special case: src == dst. We have a clean and easy solution: simply 
    //rotate the list between the two indices. By the way, trigger a redraw
    //of all rotated children
    if (src == dst) {
        if (src_ind < dst_ind) {
            twm_node *tmp = src->children[src_ind];
            int i;
            for (i = src_ind; i < dst_ind; i++) {
                src->children[i] = src->children[i + 1];
                redraw_twm_node_tree(src->children[i]);
            }
            src->children[dst_ind] = tmp;
            redraw_twm_node_tree(tmp);
        } else {
            twm_node *tmp = src->children[src_ind];
            int i;
            for (i = src; i > dst_ind; i--) {
                src->children[i] = src->children[i - 1];
                redraw_twm_node_tree(src->children[i]);
            }
            src->children[dst_ind] = tmp;
            redraw_twm_node_tree(tmp);
        }
        
        #ifdef DEBUG_ON
        fprintf(stderr, "move_node postcondition: 0x%x\n", check_tree_invariants(t->head));
        fflush(stderr);
        #endif
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //Now deal with the ugly case
    
    //First, because of the tree invariant that no node has fewer than two
    //children, one can prove that is is safe to insert this node at the
    //right place before deleting it in its old place.
    

    
    #ifdef DEBUG_ON
    fprintf(stderr, "move_node postcondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
}

//Returns 0 on success, -1 on error (and sets t->error_str). Returns -2 if
//t is NULL.
int twm_tree_move_focused_node(twm_tree *t, twm_dir dir) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (t->focus == NULL) {
        //No focus; nothing to do
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //If we are focused on the root of the tree, moving makes no sense
    if (t->focus->parent == NULL) {
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //The TWM_PARENT and TWM_CHILD directions are nonsensical
    if (dir == TWM_PARENT || dir == TWM_CHILD) {
        t->error_str = TWM_BAD_DIR;
        return -1;
    }
    
    //Now the tricky business. Following "sane" rules, move window to where
    //the user intends
    twm_node *cur = t->focus->parent;
    twm_node *prev = t->focus;
    
    int list_dir = (dir == UP || dir == LEFT) ? -1 : 1;
    int jumped_hierarchy = 0;
    
    while (cur != NULL) {
        //If we are going TWM_UP or TWM_DOWN, and cur is a TWM_HORZ node,
        //then we have to go up the heirarchy. Likewise for TWM_LEFT and
        //TWM_RIGHT in a TWM_VERT node.
        if (((dir == TWM_UP || dir == TWM_DOWN) && cur->type == TWM_HORZ) ||
            ((dir == TWM_LEFT || dir == TWM_RIGHT) && cur->type == TWM_VERT)) 
        {
            //Keep going up the heirarchy
            prev = cur;
            cur = cur->parent;
            jumped_hierarchy = 1;
            continue;
        }

        //Otherwise, try moving along the list of children
        
        //First find index of prev in cur's child list
        int ind = twm_node_indexof(prev, cur);
        if (ind < 0) {
            //Propagate error
            t->error_str = cur->error_str;
            return -1;
        }
        
        //If we are already at the end, we need to go up the hierarchy
        if (ind + list_dir < 0 || ind + list_dir >= cur->num_children) {
            //Keep going up the heirarchy
            prev = cur;
            cur = cur->parent;
            jumped_hierarchy = 1;
            continue;
        }
        
        //Finally! We have a case we can deal with. Move the window at 
        //t->focus to cur->chlidren[ind + list_dir], taking care to set 
        //focus, redraw, delete nodes, etc.
        
        //In general, the technique would be to delete the node at t->focus
        //and re-insert it at the right place. However, modifying the tree
        //can invalidate cur and ind. Ugly...
        //TODO
        //Also, make sure to treat case where we move the node down the
        //hierarchy of the node it is next to
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //There is no good place to move the window in the tree as it is. Add
    //a new row/column to the root window
    
    //TODO
    
    t->error_str = TWM_SUCC;
    return 0;
}

//Sets stack direction of active focus node. Returns -1 on error and sets
//t->error_str (or returns -2 if t was NULL)
int twm_set_stack_dir_focused(twm_tree *t, twm_node_type type) {
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    if (type != TWM_HORZ || type != TWM_VERT) {
        t->error_str = TWM_BAD_NODE_TYPE;
        return -1;
    }
    
    if (t->focus == NULL) {
        //No focus; nothing to do
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    twm_node *parent = t->focus->parent;
    //If the focused node is the root of the tree...
    if (parent == NULL) {
        //The focused node must be a LEAF and the head (as long as the tree
        //is in a valid state). Double-check this, and then make the node
        //into a stacked node
        if (t->focus->type != LEAF || t->focus != head) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        //Take this node and make it a TWM_LEAF child of a TWM_HORZ node
        twm_node *to_add = construct_leaf_twm_node(t->focus->item, t->focus->draw_ops);
        if (to_add == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        
        t->focus->type = type;
        t->num_children = 1;
        t->children[0] = to_add;
        
        //Make sure to trigger redraw
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error code
            t->error_str = t->focus->error_str;
            return -1;
        }
        
        //Success
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    twm_node *to_toggle = t->focus;
    //If the user was focused on a non-leaf node, we will toggle that node's
    //stack direction. Otherwise, we will toggle its parent's stack direction
    
    if (t->focus->type == TWM_LEAF) to_toggle = parent;
    
    if (to_toggle->type == TWM_LEAF) {
        //A leaf node cannot be toggled. This is bad!
        t->error_str = TWM_INVALID_TREE;
        return -1;
    } else if (to_toggle->type != type) {
        to_toggle->type = type;
        int rc = redraw_twm_node_tree(to_toggle);
        if (rc < 0) {
            //Propagate error
            t->error_str = to_toggle->error_str;
            return -1;
        }
        
        //Success
        t->error_str = SUCC;
        return 0;
    } 
    
    //Nothing to do
    t->error_str = TWM_SUCC;
    return -1;
}

#endif
