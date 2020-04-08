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
extern char const *const TWM_BAD_POS; // = "bad node position";q
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
extern char const *const TWM_ILLEGAL_DELETE; // = "bad delete (whole tree deletion is disabled)";
extern char const *const TWM_BAD_DEVELOPER; // = "Marco did not know what to do here";
extern char const *const TWM_NOT_LEAF; // = "node is not a leaf";
extern char const *const TWM_NO_FOCUS; // = "no focused node";
extern char const *const TWM_TYPE_MISMATCH; // = "focused node is not of desired type";

//Draws item. Returns number of bytes added into buf, or -1 on error.
typedef int draw_fn_t(void *item, int x, int y, int w, int h, char *buf);

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
typedef int draw_sz_t(void *item, int w, int h);

//Tells item that it should redraw, probably because it moved to another
//area of the screen
typedef void trigger_redraw_t(void *item);

//All drawable items must implement this interface
typedef struct _draw_operations {
    draw_fn_t *draw_fn;
    draw_sz_t *draw_sz;
    trigger_redraw_t *trigger_redraw;
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

//Invariant maintained by the functions in this library: if a node has a 
//single child, then that single child is a leaf.
typedef struct _twm_tree {
    twm_node *head;
    twm_node *focus; //Points to the window that the user is "focused" on
                     //This is used for positioning new windows, and if I
                     //later decide to draw borders differently around the
                     //focused window
    
    //Error informaiton
    char const *error_str;
} twm_tree;

//Here's the key idea: twm nodes also know how to draw themselves
int draw_fn_twm_node(void *item, int x, int y, int w, int h, char *buf);

int draw_sz_twm_node(void *item, int w, int h);

void trigger_redraw_twm_node(void *item);

extern draw_operations const empty_ops;

//Returns a new twm_tree on success, NULL on error
twm_tree* new_twm_tree();

//Frees twm_tree and all associated resources. Gracefully ignores NULL input
void del_twm_tree(twm_tree *t);

//Directions for moving focus/windows
typedef enum _twm_dir {
    TWM_UP,
    TWM_DOWN,
    TWM_LEFT,
    TWM_RIGHT,
    TWM_PARENT,
    TWM_CHILD
} twm_dir;

//Returns 0 on success, -1 on error (and sets t->error_str). Returns -2 if
//t is NULL.
int twm_tree_move_focus(twm_tree *t, twm_dir dir);

//Inserts a new drawable, based on t's currently focused window. Returns 0
//on success, or -1 on error (and sets t->error_str). Returns -2 if t was
//NULL
int twm_tree_add_window(twm_tree *t, void *item, draw_operations draw_ops);

//Removes focused node from the tree. Follows usual error return method.
int twm_tree_remove_focused(twm_tree *t);

//Returns 0 on success, -1 on error (and sets t->error_str). Returns -2 if
//t is NULL.
int twm_tree_move_focused_node(twm_tree *t, twm_dir dir);

//Sets stack direction of active focus node. Returns -1 on error and sets
//t->error_str (or returns -2 if t was NULL)
int twm_set_stack_dir_focused(twm_tree *t, twm_node_type type);

//Togles stack direction of active focus node. Returns -1 on error and sets
//t->error_str (or returns -2 if t was NULL)
int twm_toggle_stack_dir_focused(twm_tree *t);

//write()s the data described by t into fd, given the target screen size
//and coordinates. Follows usual error return technique
int twm_draw_tree(int fd, twm_tree *t, int x, int y, int w, int h);

char const* twm_tree_strerror(twm_tree *t);

//Each tree node stores a void pointer to the drawable item. However, we
//can do a cheeky trick to figure out the type pointed at by the pointer:
//check its drawing function. Ths function looks up the void pointer in
//the focused node and returns it if the drawing function matches the one
//given. It returns NULL on mismatch (or if there is an error). You may
//check t->error_str for more information
void *twm_tree_get_focused_as(twm_tree *t, draw_fn_t *draw_fn) {
    if (t == NULL) {
        return NULL; //This is all we can do
    }
    
    if (t->focus == NULL) {
        t->error_str = TWM_NO_FOCUS;
        return NULL;
    }
    
    if (t->focus->type != TWM_LEAF) {
        //Special case: sometimes a node only has one child (and our tree
        //invariant states only leaf nodes may be only children).
        if (t->focus->num_children == 1) {
            //Switch focus to the only child
            t->focus->has_focus = 0;
            t->focus = t->focus->children[0];
            if (t->focus == NULL) {
                t->error_str = TWM_INVALID_TREE;
                return NULL;
            }
            t->focus->has-focus = 1;
            if (t->focus->type != LEAF) {
                //This violates the tree invariant
                t->error_str = TWM_INVALID_TREE;
                return NULL;
            }
        } else {
            t->error_str = TWM_NOT_LEAF;
            return NULL;
        }
    }
    
    if (t->focus->draw_ops.draw_fn != draw_fn) {
        t->error_str = TWM_TYPE_MISMATCH;
        return NULL;
    }
    
    t->error_str = TWM_SUCC;
    return t->focus->item;
}

//Helper function to recurse through tree. Used by twm_tree_remove_item.
//Returns 1 if item was found and deleted, 0 if not found, -1 on error (and
//sets tree->error_str), or -2 if t is NULL
static int twm_tree_node_remove_item(twm_tree *tree, twm_node *t, void *item) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (t->type == LEAF && t->item == item) {
        //Delete this node. For reasons I won't get into, to do that we must
        //find its parent
        twm_node *parent = t->parent;
        if (parent == NULL) {
            //This is the root of the tree (but double-check)
            if (tree->head != t) {
                tree->error_str = TWM_INVALID_TREE;
                return -1;
            }
        }
        
        int ind = twm_node_indexof(t, parent);
        if (ind < 0) {
            //Propagate error
            tree->error_str = parent->error_str;
            return -1;
        }
        
        int rc = twm_remove_node(tree, parent, ind);
        if (rc == 0) {
            return 1; //Searched and destroyed
        } else {
            return rc; //tree->error_str already set
        }
    } else {
        //Recurse on each of the children
        if (t->num_children < 1) {
            tree->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        int i;
        for (i = 0; i < t->num_children; i++) {
            int rc = twm_tree_node_remove_item(tree, t->children[i], item);
            if (rc == 0) {
                //Not found, but no error
                continue;
            } else {
                return rc; //Either found or error; either way, this is the correct return value and t->error_str is set
            }
        }
        
        return 0; //Not found
    }
    
    tree->error_str = TWM_IMPOSSIBLE;
    return -1;
}

//Deletes the first tree node which contains item. Returns 0 on success,
//-2 if t was NULL, or -1 (and sets t->error_str) on error
int twm_tree_remove_item(twm_tree *t, void *item) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    int rc = twm_tree_node_remove_item(t, t->head, item);
    
    if (rc == 0) {
        //In case user wants to check
        t->error_str = TWM_NOT_FOUND;
        return 0; //But this is still not an error
    } else if (rc == 1) {
        t->error_str = TWM_SUCC;
        return 0; //All is good
    } else {
        return -1; //t->error_str already set
    }
}

//Helper function to recurse through a tree. Follows usual return code
//convention
static twm_node* find_item(twm_tree *tree, twm_node *t, void *item) {
    if 
}

//Sets the tree's focus to the first node containing item. Follows usual 
//return code pattern
int twm_tree_focus_item(twm_tree *t,

//Temporary, for debugging
int redraw_twm_node_tree(twm_node *t);
#endif
