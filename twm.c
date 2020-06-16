#ifdef DEBUG_ON
#include <stdio.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include "textio.h"
#include "twm.h"

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
    
    //Unfortunately, at this point, we don't know yet if any children will
    //want to draw themselves. So, we pre-emptively add the command to turn
    //on highlighting if we are the focused node...
    if (t->has_focus) {
        *buf++ = '\e'; *buf++ = '['; *buf++ = '1'; *buf++ = 'm'; //Turn on highlight mode
    }
    //...and the technique will be to "undo" that if nothing was drawn.
    int child_drawn = 0;
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
        } else if (child_sz > 0) {
            child_drawn = 1;
        }
        buf += child_sz;
        
        if (!child_drawn) {
            //Nothing to add, return early. This fixes a major performance
            //bug where we were always sending commands to turn highlighting
            //on and back off, even without drawing children
            t->error_str = TWM_SUCC;
            return 0;
        }
        
        if (t->has_focus) {
            *buf++ = '\e'; *buf++ = '['; *buf++ = 'm'; //Restore original mode
        }
        return buf - buf_saved;
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
            } else if (child_sz > 0) {
                child_drawn = 1;
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
        
        if (!child_drawn && !t->need_redraw) {
            //Nothing to add, return early. This fixes a major performance
            //bug where we were always sending commands to turn highlighting
            //on and back off, even without drawing children
            t->error_str = TWM_SUCC;
            return 0;
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
                child_height++;
                err -= D;
            }
            int child_sz = draw_fn_twm_node(t->children[i], x, child_y, w, child_height, buf);
            if (child_sz < 0) {
                //Propagate error upwards
                t->error_str = t->children[i]->error_str;
                return child_sz;
            } else if (child_sz > 0) {
                child_drawn = 1;
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
        
        if (!child_drawn && !t->need_redraw) {
            //Nothing to add, return early. This fixes a major performance
            //bug where we were always sending commands to turn highlighting
            //on and back off, even without drawing children
            t->error_str = TWM_SUCC;
            return 0;
        }
        
        if (t->has_focus && t->need_redraw) {
            *buf++ = '\e'; *buf++ = '['; *buf++ = 'm'; //Restore original mode
        }
        
        t->need_redraw = 0;
        
        t->error_str = TWM_SUCC;
        return buf - buf_saved;
    }
    
    t->error_str = TWM_BAD_NODE_TYPE;
    return -1;
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
        } else if (child_sz > 0 && t->has_focus) {
            total_sz += 7; //Extra characters needed to turn on highlight and
                           //ten turn it off afterwards
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
        int child_drawn = 0;
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
            } else if (child_sz > 0) {
                child_drawn = 1;
            }
            total_sz += child_sz;
            err += rem;
        }
        
        //Now add in size we need for drawing the borders, provided this 
        //node needs a redraw

        int border_sz = 0;   //Size needed for a single border line
        if (t->need_redraw) {
            border_sz += 10; //Upper bound of bytes needed to place the cursor at the top of the line
            border_sz += 1;  //The first '|' character of the border
            border_sz += 7*(h-1); //Each succeeding '|' requires 6 bytes to move the cursor to the right place, then one more for the '|' character
        }
        
        total_sz += (t->num_children - 1) * border_sz;
        
        if (child_drawn && t->has_focus) {
            total_sz += 7; //Extra characters needed to turn on highlight and
                           //then turn it off afterwards
        }
        
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
        int child_drawn = 0;
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
            } else if (child_sz > 0) {
                child_drawn = 1;
            }
            total_sz += child_sz;
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
        
        if (child_drawn && t->has_focus) {
            total_sz += 7; //Extra characters needed to turn on highlight and
                           //ten turn it off afterwards
        }
        
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

//Returns a newly allocated twm_node with given drawable, or NULL on error
static twm_node* construct_leaf_twm_node(void *item, draw_operations draw_ops) {
    twm_node *ret = calloc(1, sizeof(twm_node));
    if (!ret) return NULL;
    
    ret->type = TWM_LEAF;
    ret->item = item;
    ret->draw_ops = draw_ops;
    ret->num_children = 0; //Maybe this will come in handy? 
    
    ret->error_str = TWM_SUCC;
    return ret;
}

static twm_node* construct_twm_node(twm_node_type type) {
    twm_node *ret = calloc(1, sizeof(twm_node));
    if (!ret) return NULL;
    
    ret->type = type;
    ret->num_children = 0; //Maybe this will come in handy?
    
    ret->error_str = TWM_SUCC;
    return ret;
}

//Free twm_node, and if it is a leaf, calls the item's exit callback (if
//one was provided). Gracefully ignores NULL input
static void destroy_twm_node(twm_node *t) {
    if (t == NULL) return;
    
    if (t->type == TWM_LEAF && t->draw_ops.exit != NULL) t->draw_ops.exit(t->item);
    free(t);
}

//Free twm_node tree rooted at this node. Gracefully ignores NULL input
static void free_twm_node_tree(twm_node *head) {
    if (head == NULL) return;
    
    if (head->type == TWM_LEAF) {
        destroy_twm_node(head);
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
int redraw_twm_node_tree(twm_node *t) {
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
        t->draw_ops.trigger_redraw(t->item);
        
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
        }
            
        //Also, redraw ourselves
        t->need_redraw = 1;
        
        //Success
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    t->error_str = TWM_IMPOSSIBLE;
    return -1;
}

//Special functions for empty nodes
static int draw_fn_empty(void *item, int x, int y, int w, int h, char *buf) {return 0;}
static int draw_sz_empty(void *item, int w, int h) {return 0;}
static void trigger_redraw_empty(void *item) {return;}

draw_operations const empty_ops = {
    .draw_fn = draw_fn_empty,
    .draw_sz = draw_sz_empty,
    .trigger_redraw = trigger_redraw_empty,
    .exit = NULL
};

//Returns a new twm_tree on success, NULL on error
twm_tree* new_twm_tree() {
    twm_tree *ret = calloc(1, sizeof(twm_tree));
    
    //The head and focus pointers are already set to NULL
    ret->error_str = TWM_SUCC;
    return ret;
}

//Frees twm_tree and all associated resources. Gracefully ignores NULL input
void del_twm_tree(twm_tree *t) {
    if (t == NULL) return;
    
    free_twm_node_tree(t->head);
    
    free(t);
}

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
        int rc = redraw_twm_node_tree(t->head);
        if (rc < 0) {
            //Propagate error code
            t->error_str = t->focus->error_str;
            return -1;
        }
        return 0;
    }
    
    //If we are focused on the root of the tree, we can only move the focus
    //down the hierarchy
    if (t->focus->parent == NULL && dir != TWM_CHILD) {
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
        
        t->error_str = TWM_SUCC;
        return 0;
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
    
    int list_dir = (dir == TWM_UP || dir == TWM_LEFT) ? -1 : 1;
    
    while (cur != NULL) {
        //If we are going TWM_UP or TWM_DOWN, and cur is a TWM_HORZ node,
        //then we have to go up the heirarchy. Likewise for TWM_LEFT and
        //TWM_RIGHT in a TWM_VERT node.
        if (((dir == TWM_UP || dir == TWM_DOWN) && cur->type == TWM_HORZ) ||
            ((dir == TWM_LEFT || dir == TWM_RIGHT) && cur->type == TWM_VERT)) 
        {
            //Keep going up the hierarchy
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
            //Keep going up the hierarchy
            prev = cur;
            cur = cur->parent;
            continue;
        }
        
        //Finally! We have a case we can deal with. Focus on the predecessor 
        //of prev in the child list
        twm_node *next_focus = cur->children[ind + list_dir];
        
        //Make sure cur is non-NULL
        if (next_focus == NULL) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        //First,travel down hierarchy if cur->children[ind + list_dir] is
        //not a leaf node.
        while (next_focus->type != TWM_LEAF) {
            twm_node *child = next_focus->children[next_focus->num_children-1];
            if (child == NULL) {
                t->error_str = TWM_INVALID_TREE;
                return -1;
            }
            next_focus = child;
        }
        
        //Set focus and redraw
        t->focus->has_focus = 0;
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error upward
            t->error_str = t->focus->error_str;
            return -1;
        }
        
        next_focus->has_focus = 1;
        rc = redraw_twm_node_tree(next_focus);
        if (rc < 0) {
            //Propagate error upward
            t->error_str = next_focus->error_str;
            return -1;
        }
        
        t->focus = next_focus;
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //There is no good place to move the focus, so just do nothing
    t->error_str = TWM_SUCC;
    return 0;
}

//Follows usual error-return technique. Also makes sure to set redraws
//where necessary. DOES NOT FREE MEMORY HELD BY parent->children[ind]!
static int twm_remove_node(twm_tree *t, twm_node *parent, int ind) {
    //Sanity check inputs
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (parent == NULL) {
        t->error_str = TWM_NULL_ARG;
        return -1;
    }
    
    if (ind < 0 || ind >= parent->num_children) {
        t->error_str = TWM_OOB;
        return -1;
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "remove_node precondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    //Remove the node from the list it is in
    int i;
    for (i = ind; i < parent->num_children - 1; i++) {
        parent->children[i] = parent->children[i + 1];
    }
    parent->num_children--;
    
    //Edit the parent to make sure we don't break any imvariants. 
    //FINALLY! I found the really subtle bug that's been biting me on and
    //off for a long time. When we do this trick of copying the child into
    //its parent, we also need to move the focus if the child was focused
    //before
    if (parent->num_children == 1) {
        twm_node *tmp = parent->children[0];
        parent->type = tmp->type;
        parent->item = tmp->item;
        parent->draw_ops = tmp->draw_ops;
        if (tmp->type != TWM_LEAF) {
            parent->num_children = tmp->num_children;
            for (i = 0; i < parent->num_children; i++) {
                parent->children[i] = tmp->children[i];
                parent->children[i]->parent = parent; //That's a mouthful...
            }
        } else {
            parent->num_children = 0; //Not strictly necessary, but might help
        }
        
        tmp->type = TWM_UNINITIALIZED;
        
        if (t->focus == tmp) t->focus = parent;
        
        destroy_twm_node(tmp);
        
        //Make sure we redraw this node and all its children, which have
        //almost certainly all moved
        redraw_twm_node_tree(parent);
    } else if (parent->num_children == 0) {
        //Recursively remove the parent
        twm_node *grandparent = parent->parent;
        if (grandparent == NULL) {
            //Trying to delete the whole tree
            t->error_str = TWM_ILLEGAL_DELETE; //TODO: this isn't really an error, 
            //Basically, it's legal to delete the whole tree, but for some reason I'm scared to just
            //have the code do that. I'm not sure why
            return -1;
        }
        
        int parent_ind = twm_node_indexof(parent, grandparent);
        if (parent_ind < 0) {
            //Propagate error
            t->error_str = grandparent->error_str;
            return -1;
        }
        
        int rc = twm_remove_node(t, grandparent, parent_ind);
        if (rc < 0) {
            return -1; //t->error_str already set
        }
    } else {
        int rc = redraw_twm_node_tree(parent);
        if (rc < 0) {
            //propagate error
            t->error_str = parent->error_str;
            return -1;
        }
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "remove_node postcondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    //src is not freed!!!
    
    t->error_str = TWM_SUCC;
    return 0;
}

//Inserts the twm_node (specified as the node pointed to by src) into the
//location specified as the dst_ind'th child of dst. Follows usual error
//return method
//UGLY CORNER CASE: Normally, before calling this function, you will figure
//out a stacked node and index where you want this new node to go. However,
//if there is only one node in the tree, then it doesn't have a parent. This
//function will handle that case if the dst argument points to a LEAF node.
//In that case, dst_ind is ignored.
static int twm_insert_node(twm_tree *t, twm_node *src, twm_node *dst, int dst_ind) {
    //Sanity check inputs
    if (t == NULL) {
        return -2;
    }
    if (src == NULL || dst == NULL) {
        t->error_str = TWM_NULL_ARG;
        return -1;
    }
    
    //Corner case: if there is only one node in the tree, then it is a leaf
    //and we need to convert it to a stacked node
    if (dst->type == TWM_LEAF) {
        //This can only happen if this node is at the head of the tree. 
        //Double check this and then do the necessary changes
        if (t->head != dst) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        
        //Take this node and make it a TWM_LEAF child of a TWM_HORZ node
        twm_node *to_add = construct_leaf_twm_node(dst->item, dst->draw_ops);
        if (to_add == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        
        dst->type = TWM_HORZ;
        dst->num_children = 1;
        dst->children[0] = to_add;
        to_add->parent = dst;
        
        dst_ind = 1;
    } else if (dst_ind < 0 || dst_ind > dst->num_children) {
        t->error_str = TWM_BAD_POS;
        return -1;
    }
    
    //Now we can write the general function
    if (dst->num_children == MAX_CHILDREN) {
        t->error_str = TWM_FULL_PARENT;
        return -1;
    }
    
    dst->num_children++;
    //Shift down all the nodes that go after the inserted one
    int i;
    for (i = dst->num_children - 1; i > dst_ind; i--) {
        dst->children[i] = dst->children[i - 1];
    }
    //Put the new node in the right place
    dst->children[dst_ind] = src;
    src->parent = dst;
    
    //Make sure to trigger a redraw
    int rc = redraw_twm_node_tree(dst);
    if (rc < 0) {
        //Propagate error
        t->error_str = dst->error_str;
        return -1;
    }
    
    //Success
    t->error_str = TWM_SUCC;
    return 0;
}

//Moves a twm_node (specified as the node pointed to by src) to another
//location (specified as the dst_ind'th child of dst). Follows usual error
//return method
static int twm_move_node(twm_tree *t, twm_node *src, twm_node *dst, int dst_ind) {
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
    if (dst_ind < 0 || dst_ind >= dst->num_children) {
        t->error_str = TWM_OOB;
        return -1;
    }
    
    twm_node *parent = src->parent;
    
    if (parent == NULL) {
        //You're trying to delete the whole tree!
        t->error_str = TWM_ILLEGAL_DELETE;
        return -1;
    }
    
    int src_ind = twm_node_indexof(src, parent);
    if (src_ind < 0) {
        //Propagate error up
        t->error_str = parent->error_str;
        return -1;
    }
    
    //If src is already a child of dst, all we have to do is rotate the nodes
    //while making sure to trigger redraws.
    if (parent == dst) {
        int src_ind = twm_node_indexof(src, dst);
        if (src_ind < 0) {
            //Propagate error
            t->error_str = dst->error_str;
            return -1;
        }
        
        if (src_ind < dst_ind) {
            twm_node *tmp = dst->children[src_ind];
            int i;
            for (i = src_ind; i < dst_ind; i++) {
                dst->children[i] = dst->children[i + 1];
                redraw_twm_node_tree(dst->children[i]);
                //TODO check error codes? Although I have been checking
                //every single one up until now, I'm starting to wonder if 
                //that's really a good idea...
            }
            dst->children[dst_ind] = tmp;
            redraw_twm_node_tree(tmp); //TODO: check errors?
        } else {
            twm_node *tmp = dst->children[src_ind];
            int i;
            for (i = src_ind ; i > dst_ind; i--) {
                dst->children[i] = dst->children[i -1];
                redraw_twm_node_tree(dst->children[i]); //TODO check errors?
            }
            dst->children[dst_ind] = tmp;
            redraw_twm_node_tree(tmp); //TODO: check errors?
        }
        
        //Success
        t->error_str = TWM_SUCC;
        return 0;
    }
      
    //First, place the node where we want it to go. After this call, there
    //will effectively be two copies of the node in the tree. We do this
    //because we must guarantee that dst and dst_ind remain valid at the
    //time of insertion, but we don't care what happens to them later.
    int rc = twm_insert_node(t, src, dst, dst_ind);
    if (rc < 0) {
        return -1; //t->error_str is already set
    }
    
    //Now we delete parent->children[src_ind] from the tree. This removes 
    //the extra copy. It can also invalidate dst and dst_ind, but we are no
    //longer using them, so it's not a problem.
    rc = twm_remove_node(t, parent, src_ind);
    if (rc < 0) {
        return -1; //t->error_str is already set
    }
    
    #ifdef DEBUG_ON
    fprintf(stderr, "move_node postcondition: 0x%x\n", check_tree_invariants(t->head));
    fflush(stderr);
    #endif
    
    //Success
    t->error_str = TWM_SUCC;
    return 0;
}

//Inserts a new drawable, based on t's currently focused window. Returns 0
//on success, or -1 on error (and sets t->error_str). Returns -2 if t was
//NULL
int twm_tree_add_window(twm_tree *t, void *item, draw_operations draw_ops) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    //Allocate the new node
    twm_node *to_add = construct_leaf_twm_node(item, draw_ops);
    if (to_add == NULL) {
        t->error_str = TWM_OOM;
        return -1;
    }
    
    //Special case: inserting into empty tree
    if (t->head == NULL) {
        //Tree is empty, so just put in this new node
        t->head = to_add;
        t->focus = to_add;
        to_add->has_focus = 1;
        redraw_twm_node_tree(t->head);
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //This just makes sure to cover a corner case before moving onto the
    //general code
    if (t->focus == NULL) {
        t->focus = t->head;
    }
    
    //Guarantee: t->focus is non-NULL
    
    //Actually insert the node. The user can be focused on a leaf node or
    //on any non-leaf node in the tree. Find the proper node which will 
    //become the parent of the newly inserted node.
    twm_node *dst = t->focus;
    int dst_ind = dst->num_children;
    
    
    if (dst->type == TWM_LEAF) {
        twm_node *parent = dst->parent;
        if (parent == NULL) {
            //Special case: inserting node into a tree with only one 
            //existing node. I cover this case in twm_insert_node.
            int rc = twm_insert_node(t, to_add, dst, 0); //Last argument ignored in this special case
            if (rc < 0) {
                return -1; //t->error_str is already set
            }
            
            t->focus->has_focus = 0;
            t->focus = to_add;
            to_add->has_focus = 1;
            //Success
            t->error_str = TWM_SUCC;
            return 0;
        } 
        
        dst = parent;
        dst_ind = twm_node_indexof(t->focus, dst) + 1; //Place new win after focused
        if (dst_ind < 0) {
            //Propagate error up
            t->error_str = dst->error_str;
            return -1;
        }
    }
    
    //At this point, dst is the parent of the newly inserted node and dst_ind
    //is where to put it
    int rc = twm_insert_node(t, to_add, dst, dst_ind);
    if (rc < 0) {
        return -1; //t->error_str already set
    }
    
    t->focus->has_focus = 0;
    t->focus = to_add;
    to_add->has_focus = 1;

    //Success
    t->error_str = TWM_SUCC;
    return 0;
}

//Removes focused node from the tree. Follows usual error return method.
int twm_tree_remove_focused(twm_tree *t) {
    if (t == NULL) {
        return -2;
    }
    
    if (t->focus == NULL) {
        //Nothing to do
        return 0;
    }
    
    twm_node *parent = t->focus->parent;
    
    if (parent == NULL) {
        //Trying to delete the whole tree. Handle this easy case and return 
        //early, but make sure that this is really the case
        if (t->head != t->focus) {
            t->error_str = TWM_INVALID_TREE;
            return -1;
        }
        free_twm_node_tree(t->head);
        t->head = NULL;
        t->focus = NULL;
        return 0;
    }
    
    //Try to find a reasonable node to focus on
    twm_node *next_focus;
    
    int focus_ind = twm_node_indexof(t->focus, parent);
    if (focus_ind < 0) {
        //Propagate error code
        t->error_str = parent->error_str;
        return -1;
    }

    //Ugly corner case: if this node is the only child of another node,
    //then that other node is going to be deleted when this one is. So,
    //we can't focus to that one.
    if (parent->num_children == 1) {
        twm_node *grandparent = parent->parent;
        if (grandparent == NULL) {
            //I don't know what this means for us, so return an error
            t->error_str = TWM_BAD_DEVELOPER;
            return -1;
        }
        
        int parent_ind = twm_node_indexof(parent, grandparent);
        if (parent_ind < 0) {
            //Propagate error
            t->error_str = grandparent->error_str;
            return -1;
        }
        
        int ind = (parent_ind + 1) % grandparent->num_children;
        next_focus = grandparent->children[ind];
    } else if (parent->num_children == 2) {
        //On deletion, the other child of this node will be merged into
        //the parent. So next_focus should just be the parent
        next_focus = parent;
    } else {
        int ind = (focus_ind + 1) % parent->num_children;
        next_focus = parent->children[ind];
    }
    
    //We have found a suitable node to focus on. Delete the old one
    int rc = twm_remove_node(t, parent, focus_ind);
    if (rc < 0) {
        return -1; //twm_remove_node has already set the error code
    }
    
    //Release resources from deleted subtree
    free_twm_node_tree(t->focus);
    
    //Make sure to set focus and redraw
    t->focus = next_focus;
    t->focus->has_focus = 1;
    
    rc = redraw_twm_node_tree(parent);
    if (rc < 0) {
        //Propagate error code
        t->error_str = parent->error_str;
        return -1;
    }
    
    //Success
    t->error_str = TWM_SUCC;
    return 0;
}

//Returns 0 on success, -1 on error (and sets t->error_str). Returns -2 if
//t is NULL.
int twm_tree_move_focused_node(twm_tree *t, twm_dir dir) {
    //Sanity check inputs
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
    
    int list_dir = (dir == TWM_UP || dir == TWM_LEFT) ? -1 : 1;
    
    while (cur != NULL) {
        //If we are going TWM_UP or TWM_DOWN, and cur is a TWM_HORZ node,
        //then we have to go up the heirarchy. Likewise for TWM_LEFT and
        //TWM_RIGHT in a TWM_VERT node.
        if (((dir == TWM_UP || dir == TWM_DOWN) && cur->type == TWM_HORZ) ||
            ((dir == TWM_LEFT || dir == TWM_RIGHT) && cur->type == TWM_VERT)) 
        {
            //Keep going up the hierarchy
            prev = cur;
            cur = cur->parent;
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
            //Keep going up the hierarchy
            prev = cur;
            cur = cur->parent;
            continue;
        }
        
        //Finally! We have a case we can deal with. Move the window at 
        //t->focus to cur->chlidren[ind + list_dir], taking care to set 
        //focus, redraw, delete nodes, etc.
        
        twm_node *insertion_parent = cur;
        int insertion_ind = ind + list_dir;
        
        //However, first check if we need to move down a hierarchy.
        twm_node *adjacent = cur->children[ind + list_dir];
        if (adjacent->type != TWM_LEAF) {
            //Travel down hierarchy until we find the correct place
            if (list_dir == 1) {
                //Find "leftmost" child
                twm_node *parent = adjacent; 
                while (1) {
                    if (parent->num_children < 1) {
                        t->error_str = TWM_INVALID_TREE;
                        return -1;
                    }
                    twm_node *lchild = parent->children[0];
                    if (lchild == NULL) {
                        t->error_str = TWM_INVALID_TREE;
                        return -1;
                    }
                    
                    if (lchild->type == TWM_LEAF) {
                        insertion_parent = parent;
                        insertion_ind = 0;
                        break;
                    }
                    
                    parent = lchild;
                }
            } else {
                //Find "rightmost" child
                twm_node *parent = adjacent; 
                while (1) {
                    if (parent->num_children < 1) {
                        t->error_str = TWM_INVALID_TREE;
                        return -1;
                    }
                    twm_node *rchild = parent->children[parent->num_children - 1];
                    if (rchild == NULL) {
                        t->error_str = TWM_INVALID_TREE;
                        return -1;
                    }
                    
                    if (rchild->type == TWM_LEAF) {
                        insertion_parent = parent;
                        insertion_ind = 0;
                        break;
                    }
                    
                    parent = rchild;
                }
            }
        }
        
        int rc = twm_move_node(t, t->focus, insertion_parent, insertion_ind);
        if (rc < 0) {
            return -1; //t->error_str is already set
        }
        
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    //There is no good place to move the window in the tree as it is. Add
    //a new row/column to the root window
    
    //By the way, the sanity checking at the top of the function makes sure
    //we are not at the root of the tree.
    
    //First, remove the focused node from where it used to be
    twm_node *parent = t->focus->parent;
    int focus_ind = twm_node_indexof(t->focus, parent);
    if (focus_ind < 0) {
        //Propagate error code
        t->error_str = parent->error_str;
        return -1;
    }
    int rc = twm_remove_node(t, parent, focus_ind);
    if (rc < 0) {
        return -1; //error_str already set
    }
    
    twm_node *new_head;
    switch (dir) {
    case TWM_UP:
        new_head = construct_twm_node(TWM_VERT);
        if (new_head == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        new_head->num_children = 2;
        new_head->children[0] = t->focus;
        t->focus->parent = new_head;
        new_head->children[1] = t->head;
        t->head->parent = new_head;
        t->head = new_head;
        rc = redraw_twm_node_tree(new_head);
        if (rc < 0) {
            //Propagate error
            t->error_str = new_head->error_str;
        }
        break;
    case TWM_DOWN:
        new_head = construct_twm_node(TWM_VERT);
        if (new_head == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        new_head->num_children = 2;
        new_head->children[0] = t->head;
        t->head->parent = new_head;
        new_head->children[1] = t->focus;
        t->focus->parent = new_head;
        t->head = new_head;
        rc = redraw_twm_node_tree(new_head);
        if (rc < 0) {
            //Propagate error
            t->error_str = new_head->error_str;
        }
        break;
    case TWM_LEFT:
        new_head = construct_twm_node(TWM_HORZ);
        if (new_head == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        new_head->num_children = 2;
        new_head->children[0] = t->focus;
        t->focus->parent = new_head;
        new_head->children[1] = t->head;
        t->head->parent = new_head;
        t->head = new_head;
        rc = redraw_twm_node_tree(new_head);
        if (rc < 0) {
            //Propagate error
            t->error_str = new_head->error_str;
        }
        break;
    case TWM_RIGHT:
        new_head = construct_twm_node(TWM_HORZ);
        if (new_head == NULL) {
            t->error_str = TWM_OOM;
            return -1;
        }
        new_head->num_children = 2;
        new_head->children[0] = t->head;
        t->head->parent = new_head;
        new_head->children[1] = t->focus;
        t->focus->parent = new_head;
        t->head = new_head;
        rc = redraw_twm_node_tree(new_head);
        if (rc < 0) {
            //Propagate error
            t->error_str = new_head->error_str;
        }
        break;
    default:
        t->error_str = TWM_BAD_DIR;
        return -1;
    }

    //Success!
    t->error_str = TWM_SUCC;
    return 0;
}

static int wrap_leaf(twm_node *t) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    //Take this node and make it a TWM_LEAF child of a TWM_HORZ node
    twm_node *to_add = construct_leaf_twm_node(t->item, t->draw_ops);
    if (to_add == NULL) {
        t->error_str = TWM_OOM;
        return -1;
    }
    
    t->type = TWM_HORZ;
    t->num_children = 1;
    t->children[0] = to_add;
    
    to_add->parent = t;
    
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
    if (type != TWM_HORZ && type != TWM_VERT) {
        t->error_str = TWM_BAD_NODE_TYPE;
        return -1;
    }
    if (t->focus == NULL) {
        //No focus; nothing to do
        t->error_str = TWM_SUCC;
        return 0;
    }
    
    if (t->focus->type == TWM_LEAF) {
        //Special rule: if the user tries to set a leaf node's stack 
        //direction, then create a new stacked node and put this leaf as
        //its child. However, we don't want to do that if the leaf node is
        //already the single child of a node
        
        twm_node *parent = t->focus->parent;
        //We wrap the node if it has no parent or if it is not an only child
        if (parent == NULL || parent->num_children > 1) {                
            int rc = wrap_leaf(t->focus);
            if (rc < 0) {
                //Propagate error
                t->error_str = t->focus->error_str;
                return -1;
            }
            t->focus->type = type;
        } else {
            parent->type = type;
            t->focus->has_focus = 0;
            t->focus = parent;
            t->focus->has_focus = 1;
        }
            
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
    } else if (t->focus->type != type) {
        t->focus->type = type;
        int rc = redraw_twm_node_tree(t->focus);
        if (rc < 0) {
            //Propagate error
            t->error_str = t->focus->error_str;
            return -1;
        }
        
        //Success
        t->error_str = TWM_SUCC;
        return 0;
    } 
    
    //Nothing to do
    t->error_str = TWM_SUCC;
    return 0;
}

//Togles stack direction of active focus node. Returns -1 on error and sets
//t->error_str (or returns -2 if t was NULL)
int twm_toggle_stack_dir_focused(twm_tree *t) {
    if (!t) return -2;
    
    if (t->focus == NULL) {
        //Nothing to do
        return 0;
    }
    
    twm_node_type type = t->focus->type;
    if (type == TWM_VERT) {
        return twm_set_stack_dir_focused(t, TWM_HORZ);
    } else if (type == TWM_HORZ) {
        return twm_set_stack_dir_focused(t, TWM_VERT);
    } else {
        //Let twm_set_stack_dir_focused handle the edge cases
        return twm_set_stack_dir_focused(t, TWM_HORZ);
    }
    
    t->error_str = TWM_IMPOSSIBLE;
    return -1;
}

//write()s the data described by t into fd, given the target screen size
//and coordinates. Follows usual error return technique
int twm_draw_tree(int fd, twm_tree *t, int x, int y, int w, int h) {
    if (t == NULL) {
        return -2;
    }
    
    if (x < 0 || y < 0) {
        t->error_str = TWM_BAD_POS;
        return -1;
    }
    
    if (w < 0 || h < 0) {
        t->error_str = TWM_BAD_SZ;
        return -1;
    }
    
    if (t->head == NULL) {
        //This is an empty tree. For now, just do nothing
        return 0;
    }
    
    int bytes_needed = draw_sz_twm_node(t->head, w, h);
    if (bytes_needed < 0) {
        //Propagate error string
        t->error_str = t->head->error_str;
        return -1;
    } else if (bytes_needed == 0) {
        return 0; //Nothing to do
    }
    
    char *buf = malloc(bytes_needed);
    int len = draw_fn_twm_node(t->head, x, y, w, h, buf);
    
    if (len < 0) {
        free(buf);
        //Propagate error string
        t->error_str = t->head->error_str;
        return -1; //t->error_str already set
    }
    
    //TODO: should this be in a while loop in case we can't send it all at
    //once?
    int rc = write(fd, buf, len);
    if (rc < 0) {
        t->error_str = strerror(errno);
    }
    
    free(buf);
    return rc;
}

char const* twm_tree_strerror(twm_tree *t) {
    if (t == NULL) {
        return NULL;
    }
    
    return t->error_str;
}

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
            t->focus->has_focus = 1;
            if (t->focus->type != TWM_LEAF) {
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
//TODO: there are some problems with this function. It doesn't actually set
//the focus (I forgot to do that) and also I deliberately asked remove_node
//to not free node memory; this means remove_item needs to free things
//sometimes. Anyway, it'll be worth single-stepping this sucker in gdb, or
//maybe rewriting it in terms of my other functions which seem to be working
//alright
static int twm_tree_node_remove_item(twm_tree *tree, twm_node *t, void *item) {
	#warning This function has significant code stink. Really need to clean this up
    if (tree == NULL) {
        return -2; //This is all we can do
    }
    
    if (t == NULL) {
        tree->error_str = TWM_NULL_ARG;
        return -1;
    }
    
    if (t->type == TWM_LEAF) {
        if (t->item == item) {	
            //Ugly corner case #1: if the item is the only node in the tree
            twm_node *parent = t->parent;
            if (parent == NULL) {
                //This is the root of the tree (but double-check)
                if (tree->head != t) {
                    tree->error_str = TWM_INVALID_TREE;
                    return -1;
                }
                //In this case we leave the tree empty
                destroy_twm_node(t);
                tree->head = NULL;
                tree->focus = NULL;
                tree->error_str = TWM_SUCC;
                return 0;
            }	
            
            
            //Ugly corner case #2: need to use our special-purpose function 
            //for deleting this node if it is the focused node. I've said
            //this somewhere before, but I'm no longer willing to make large
            //structural changes, even if it's for the better. The special
            //twm_tree_remove_focused was a band-aid fix, but it's an 
            //effective one
            if (t == tree->focus) {
                int rc = twm_tree_remove_focused(tree);
                if (rc == 0) return 1;
                else return rc; //twm_tree_remove_focused has already set error_str
            }
            
            //Delete this node. For reasons I won't get into, to do that we must
            //find its parent
            int ind = twm_node_indexof(t, parent);
            if (ind < 0) {
                //Propagate error
                tree->error_str = parent->error_str;
                return -1;
            }
            
            int rc = twm_remove_node(tree, parent, ind);
            if (rc == 0) {
                destroy_twm_node(t);
                return 1; //Searched and destroyed
            } else {
                return rc; //tree->error_str already set
            }
        } else {
            return 0; //Not found, but no error
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
    
    //If we happen to be focused on the node we wish to delete, run our
    //special function that also computes a sane node to focus on after this
    //one is deleted.
    //Now, with the benefit of hindisght I would have coded these functions
    //differently. Oh well, this is good enough!
    if (t->focus != NULL && t->focus->type == TWM_LEAF && t->focus->item == item) {
        return twm_tree_remove_focused(t);
    }
    
    //Run our recursive function that searches and destroys
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

//Helper function to recurse through a tree. Returns NULL if not found or
//on error (tree->error_str is set accordingly)
static twm_node* find_item(twm_tree *tree, twm_node *t, void *item) {
    if (tree == NULL) {
        return NULL;
    }
    
    if (t == NULL) {
        tree->error_str = TWM_NULL_ARG;
        return NULL;
    }
    
    if (t->type == TWM_LEAF) {
        if (t->item == item) {
            return t;
        } else {
            tree->error_str = TWM_NOT_FOUND;
            return NULL;
        }
    } else {
        //Recursively search children
        if (t->num_children < 1) {
            tree->error_str = TWM_INVALID_TREE;
            return NULL;
        }
        
        int i;
        for (i = 0; i < t->num_children; i++) {
            twm_node *res = find_item(tree, t->children[i], item);
            if (res == NULL && tree->error_str != TWM_NOT_FOUND) {
                return NULL; //An error happened
            } else if (res != NULL) {
                return res; //Found it!
            }
        }
        
        tree->error_str = TWM_NOT_FOUND;
        return NULL;
    }
    
    tree->error_str = TWM_IMPOSSIBLE;
    return NULL;
}

//Sets the tree's focus to the first node containing item. Returns 0 on 
//success, -1 if item was not found (or another error occurred; check
//t->error_str). Returns -2 if t was NULL
int twm_tree_focus_item(twm_tree *t, void *item) {
    if (t == NULL) {
        return -2;
    }
    
    if (t->head == NULL) {
        t->error_str = TWM_NOT_FOUND;
        return -1;
    }
    
    twm_node *new_focus = find_item(t, t->head, item);
    if (new_focus == NULL) {
        return -1; //t->error_str is already set
    }
    
    if (t->focus != NULL) {
        t->focus->has_focus = 0;
        redraw_twm_node_tree(t->focus); //TODO: bother checking errors?
    }
    
    t->focus = new_focus;
    t->focus->has_focus = 1;
    redraw_twm_node_tree(t->focus); //TODO: bother checking errors?
    
    t->error_str = TWM_SUCC;
    return 0;
}

//Forces entire tree to redraw. Follows usual return code convention
int twm_tree_redraw(twm_tree *t) {
    if (t == NULL) {
        return -2; //This is all we can do
    }
    
    if (t->head == NULL) {
        t->error_str = TWM_NO_WINDOWS;
        return -1;
    }
    
    int rc = redraw_twm_node_tree(t->head);
    if (rc < 0) {
        //Propagate error code
        t->error_str = t->head->error_str;
        return -1;
    }
    
    t->error_str = TWM_SUCC;
    return 0;
}

//////////////////////////////////////////////////
//Error codes, which double as printable strings//
//////////////////////////////////////////////////

char const *const TWM_SUCC = "success";
char const *const TWM_LEAF_SZ_ERR = "TWM_LEAF node size check failed";
char const *const TWM_LEAF_DRAW_ERR = "TWM_LEAF node draw failed";
char const *const TWM_LEAF_TRIG_REDRAW_ERR = "TWM_LEAF node redraw trigger failed";
char const *const TWM_BAD_NODE_TYPE = "invalid node type (uninitialized memory?)";
char const *const TWM_BAD_SZ = "bad node size";
char const *const TWM_BAD_POS = "bad node position";
char const *const TWM_FULL_PARENT = "too many children";
char const *const TWM_NULL_DRAW_FN = "window has no draw function";
char const *const TWM_NULL_DRAW_SZ = "window has no size-finding function";
char const *const TWM_NULL_TRIG_REDRAW = "window has no redraw trigger function";
char const *const TWM_NO_WINDOWS = "no windows in tree";
char const *const TWM_INVALID_TREE = "window tree is in a broken state!";
char const *const TWM_NOT_FOUND = "could not find child in list";
char const *const TWM_IMPOSSIBLE = "TWM code reached a place Marco thought was impossible";
char const *const TWM_NULL_ARG = "NULL argument where non-NULL was expected";
char const *const TWM_BAD_DIR = "bad direction";
char const *const TWM_OOM = "out of memory";
char const *const TWM_OOB = "out of bounds";
char const *const TWM_ILLEGAL_DELETE = "illegal node deletion";
char const *const TWM_BAD_DEVELOPER = "Marco did not know what to do here";
char const *const TWM_NOT_LEAF = "node is not a leaf";
char const *const TWM_NO_FOCUS = "no focused node";
char const *const TWM_TYPE_MISMATCH = "focused node is not of desired type";
