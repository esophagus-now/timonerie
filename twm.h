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
extern char const *const TWM_ILLEGAL_DELETE; // = "illegal node deletion";

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

//write()s the data described by t into fd, given the target screen size
//and coordinates. Follows usual error return technique
int twm_draw_tree(int fd, twm_tree *t, int x, int y, int w, int h);

char const* twm_tree_strerror(twm_tree *t);


//Temporary, for debugging
int redraw_twm_node_tree(twm_node *t);
#endif
