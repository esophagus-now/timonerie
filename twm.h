#ifndef TWM_H
#define TWM_H 1

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

extern char const *const TWM_SUCC; // = "success";
extern char const *const TWM_LEAF_SZ_ERR; // = "leaf node size check failed";
extern char const *const TWM_LEAF_DRAW_ERR; // = "leaf node draw failed";
extern char const *const TWM_BAD_NODE_TYPE; // = "invalid node type (uninitialized memory?)";
extern char const *const TWM_BAD_SZ; // = "bad node size";
extern char const *const TWM_BAD_POS; // = "bad node position";

//Draws item. Returns number of bytes added into buf, or -1 on error.
typedef int draw_fn_t(void *item, int x, int y, int w, int h, char *buf);

//Returns how many bytes are needed (can be an upper bound) to draw item
//given the size
typedef int draw_sz_t(void *item, int w, int h);

//Tells item that it shoudl redraw, probably because it moved to another
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
    LEAF,
    HORZ,
    VERT
} twm_node_type;

#define MAX_CHILDREN 8
typedef struct _twm_node {
    //If this is a leaf node, then draw the item pointed to by item_to_draw
    //using the function pointed at by how_to_draw_it. Otherwise, we need 
    //to recurse and draw each of the children
    twm_node_type type;
    
    //This means that this node's borders will be redrawn. Has no bearing 
    //on children, and is unused when type == LEAF
    int need_redraw;
    
    //If this is a leaf, simply draw the item. The pointer and function
    //pointers contain all the necessary instructions to do it.
    void *item;
    draw_operations draw_ops;
    
    //Otherwise, this node either contains vertically or horizontally 
    //stacked "windows"
    struct _twm_node *children[MAX_CHILDREN];
    int num_children;
    
    //Error information
    char const *error_str;
} twm_node;

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
    if (t->type == LEAF) {
        //Return bytes consumed by the leaf node
        int child_sz = t->draw_ops.draw_fn(t->item, x, y, w, h, buf);
        if (child_sz < 0) {
            t->error_str = TWM_LEAF_DRAW_ERR;
        }
        return child_sz;
    } else if (t->type == HORZ) {
        //Save current buf pos, so we can calculate number of bytes we added
        char *buf_saved = buf;
        
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
        
        t->need_redraw = 0;
        
        t->error_str = TWM_SUCC;
        return buf - buf_saved;
    } else if (t->type == VERT) {
        //Save current buf pos, so we can calculate number of bytes we added
        char *buf_saved = buf;
        
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
    
    //Calculate size
    if (t->type == LEAF) {
        //Simply return the amount of space needed by the contained drawable
        int child_sz = t->draw_ops.draw_sz(t->item, w, h);
        if (child_sz < 0) {
            t->error_str = TWM_LEAF_SZ_ERR;
        }
        return child_sz;
    } else if (t->type == HORZ) {
        int total_sz = 0;
        
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
    } else if (t->type == VERT) {
        int total_sz = 0;
        
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


#endif
