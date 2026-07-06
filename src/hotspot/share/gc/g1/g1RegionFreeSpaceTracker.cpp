
#include "gc/g1/g1AllocRegion.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1RegionFreeSpaceTracker.hpp"
#include "logging/log.hpp"


G1RegionFreeSpaceTracker::G1RegionFreeSpaceTracker(G1CollectedHeap* heap) :
    _g1h(heap),
    _holes_young(nullptr),
    _holes_old(nullptr),
    _root_young(nullptr),
    _root_old(nullptr),
    _size(0),
    _min_hole_size_young(0),
    _min_hole_size_old(0),
    _use_tree(true)
{
    log_trace(gc_testing)("Init G1RegionFreeSpaceTracker");


    log_trace(gc_testing)("Size of ListHole: %ld", sizeof(ListHole));
    log_trace(gc_testing)("Size of TreeHole: %ld", sizeof(TreeHole));

}

G1RegionFreeSpaceTracker::~G1RegionFreeSpaceTracker() {
}

void G1RegionFreeSpaceTracker::initialize() {

    _size = _g1h->max_num_regions();

    _holes_young = NEW_C_HEAP_ARRAY(HeapWord*, _size, mtGC);
    _holes_old = NEW_C_HEAP_ARRAY(HeapWord*, _size, mtGC);

    for (uint i = 0; i < _size; i++) {
        _holes_young[i] = nullptr;
        _holes_old[i] = nullptr;
    }

    log_trace(gc_testing)("Create hole array (%u regions)", _g1h->max_num_regions());

    if (_use_tree) {
        // Use tree structure
        size_t words_for_struct = (sizeof(TreeHole) + sizeof(HeapWord) - 1) / sizeof(HeapWord);
        _min_hole_size_young = CollectedHeap::min_fill_size() + words_for_struct + 10; // arbitrary bonus offset
        _min_hole_size_old   = CollectedHeap::min_fill_size() + words_for_struct + 10; // arbitrary bonus offset
    } else {
        // Use list structure
        size_t words_for_struct = (sizeof(ListHole) + sizeof(HeapWord) - 1) / sizeof(HeapWord);
        _min_hole_size_young = CollectedHeap::min_fill_size() + words_for_struct + 10; // arbitrary bonus offset
        _min_hole_size_old   = CollectedHeap::min_fill_size() + words_for_struct + 10; // arbitrary bonus offset
    }

}

void G1RegionFreeSpaceTracker::add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size) {
    size_t size_in_words = size / HeapWordSize;

    if (size_in_words < _min_hole_size_young) {
        return;
    }
    
    add_hole_list(_holes_young, region, word, size_in_words);
    if (_use_tree) {        
        add_hole_tree(_root_young, word, size_in_words);
    } 
    log_trace(gc_testing)("\tAdd hole (YOUNG) at: " PTR_FORMAT ", size = %7lu", p2i(word), size_in_words);
}

void G1RegionFreeSpaceTracker::add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
   
    log_trace(gc_testing)("Humgouns region with hole of size: %lu", size_in_words);

    if (size_in_words < _min_hole_size_old) {
        log_trace(gc_testing)("%ld < %ld", size_in_words, _min_hole_size_old);
        return;
    }

    add_hole_list(_holes_old, region, word, size_in_words);
    if (_use_tree) {
        add_hole_tree(_root_old, word, size_in_words);
    } 

    log_trace(gc_testing)("\tAdd hole (OLD) at: " PTR_FORMAT ", size = %7lu", p2i(word), size_in_words);
}

void G1RegionFreeSpaceTracker::add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {    
    if (size_in_words < _min_hole_size_old) {
        return;
    }

    add_hole_list(_holes_old, region, word, size_in_words);
    if (_use_tree) {
        add_hole_tree(_root_old, word, size_in_words);
    } 
    
    log_trace(gc_testing)("\tAdd hole (OLD) at: " PTR_FORMAT ", size = %7lu", p2i(word), size_in_words);
}


void G1RegionFreeSpaceTracker::set_hole_list(HeapWord* word, size_t size, HeapWord* next) {
    int header_size = CollectedHeap::min_fill_size();
    *(ListHole*)(word + header_size) = ListHole {size, next};
}

ListHole* G1RegionFreeSpaceTracker::get_hole_list(HeapWord* word) const {
    int header_size = CollectedHeap::min_fill_size();
    return (ListHole*)(word + header_size);
}

void G1RegionFreeSpaceTracker::set_hole_tree(HeapWord* word) {
    int header_size = CollectedHeap::min_fill_size();
    *(TreeHole*)(word + header_size) = TreeHole {0, nullptr, nullptr, nullptr, nullptr, true};
}

TreeHole* G1RegionFreeSpaceTracker::get_hole_tree(HeapWord* word) const {
    int header_size = CollectedHeap::min_fill_size();
    return (TreeHole*)(word + header_size);
}

// size and next are aligned for ListHole and TreeHole
// so treating those felds as ListHoles works in both scenarios
void G1RegionFreeSpaceTracker::set_size(HeapWord* word, size_t size) {
    ListHole* hole = get_hole_list(word);
    hole->size = size;
}

size_t G1RegionFreeSpaceTracker::get_size(HeapWord* word) const {
    return get_hole_list(word)->size;
}

void G1RegionFreeSpaceTracker::set_next(HeapWord* word, HeapWord* next) {
    ListHole* hole = get_hole_list(word);
    hole->next = next;
}

HeapWord* G1RegionFreeSpaceTracker::get_next(HeapWord* word) const {
    return get_hole_list(word)->next;
}

void G1RegionFreeSpaceTracker::set_parent(HeapWord* word, HeapWord* parent) {
    TreeHole* hole = get_hole_tree(word);
    hole->parent = parent;
}

HeapWord* G1RegionFreeSpaceTracker::get_parent(HeapWord* word) const {
    return get_hole_tree(word)->parent;
}

void G1RegionFreeSpaceTracker::set_left(HeapWord* word, HeapWord* left) {
    TreeHole* hole = get_hole_tree(word);
    hole->left = left;
}

HeapWord* G1RegionFreeSpaceTracker::get_left(HeapWord* word) const {
    return get_hole_tree(word)->left;
}

void G1RegionFreeSpaceTracker::set_right(HeapWord* word, HeapWord* right) {
    TreeHole* hole = get_hole_tree(word);
    hole->right = right;
}

HeapWord* G1RegionFreeSpaceTracker::get_right(HeapWord* word) const {
    return get_hole_tree(word)->right;
}

void G1RegionFreeSpaceTracker::set_color(HeapWord* word, bool red) {
    if (word == nullptr) return;
    TreeHole* hole = get_hole_tree(word);
    hole->red = red;
}

bool G1RegionFreeSpaceTracker::get_color(HeapWord* word) const {
    if (word == nullptr) return 0;
    return get_hole_tree(word)->red; 
}

void G1RegionFreeSpaceTracker::set_red(HeapWord* word) {
    set_color(word, 1);
}

bool G1RegionFreeSpaceTracker::is_red(HeapWord* word) const {
    if (word == nullptr) return 0;
    return get_color(word);
}

void G1RegionFreeSpaceTracker::set_black(HeapWord* word) {
    set_color(word, 0);
}

bool G1RegionFreeSpaceTracker::is_black(HeapWord* word) const {
    if (word == nullptr) return 0;
    return !get_color(word);
}

void G1RegionFreeSpaceTracker::add_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
    HeapWord* next = list[region->hrm_index()];
    list[region->hrm_index()] = word;
    set_hole_list(word, size_in_words, next);
}

void G1RegionFreeSpaceTracker::add_hole_tree(HeapWord* &root, HeapWord* word, size_t size_in_words) {
    
    set_hole_tree(word);

    if (root == nullptr) {
        root = word;
        set_parent(word, nullptr);
        set_black(word);
    } else {

        HeapWord* curr = root;
        HeapWord* prev = root;

        while (curr != nullptr) {
            // Follow left
            if (size_in_words <= get_size(curr)) {

                if (curr == word) {
                    log_trace(gc_testing)("Hole already inserted");
                    return;
                } 

                prev = curr;
                curr = get_left(curr);
            }
            // Follow right
            else {
                prev = curr;
                curr = get_right(curr);
            }     
        }

        // Insert
        set_parent(word, prev);
        set_red(word);

        if (size_in_words <= get_size(prev)) {
            set_left(prev, word);
        } else {
            set_right(prev, word);
        }
    }

    set_size(word, size_in_words);
    set_left(word, nullptr);
    set_right(word, nullptr);

    // If parent is red, tree might need to be fixed
    if (is_red(get_parent(word))) {
        rb_insert_fixup(root, word);
    }

    if (size_in_words == 1600) {
        /*log_trace(gc_testing)("Remove hole with size: 900");
        HeapWord* found = find_exact_hole(root, 900);
        log_trace(gc_testing)("Found   : " PTR_FORMAT " (%s)", p2i(found), found != nullptr ? "true" : "false");
        HeapWord* removed = remove_hole_tree(root, found);
        log_trace(gc_testing)("Removed : " PTR_FORMAT " (%s)", p2i(removed), removed == found? "true" : "false");*/
        
        log_trace(gc_testing)("Start traversal");
        inorder_traversal(root);
        log_trace(gc_testing)("End traversal");
        //log_trace(gc_testing)("Find best fitting hole for size: 950");
        //HeapWord* best = find_best_fitting_hole(root, 950);
        //log_trace(gc_testing)("Best fitting hole");
        //dump_tree_node(best);
    }
}

void G1RegionFreeSpaceTracker::transplant(HeapWord* &root, HeapWord* u, HeapWord* v) {
    assert(u != nullptr, "u must not be null");
    assert(get_left(u) == nullptr || get_right(u) == nullptr, "u has at most 1 child");

    HeapWord* parent = get_parent(u);

    if (parent == nullptr) {
        root = v;
    } else if (is_left(u)) {
        set_left(parent, v);
    } else {
        set_right(parent, v);
    }
    if (v != nullptr) {
        set_parent(v, parent);
    }
}

bool G1RegionFreeSpaceTracker::is_left(HeapWord* word) const {
    HeapWord* parent = get_parent(word);
    if (parent != nullptr) {
        return get_left(parent) == word;
    }
    return false;
}

bool G1RegionFreeSpaceTracker::is_right(HeapWord* word) const {
    HeapWord* parent = get_parent(word);
    if (parent != nullptr) {
        return get_right(parent) == word;
    }
    return false;
}

void G1RegionFreeSpaceTracker::rb_insert_fixup(HeapWord* &root, HeapWord* word) {

    HeapWord* curr = word;

    while (is_red(get_parent(curr))) {
        
        HeapWord* parent = get_parent(curr);
        HeapWord* grandparent = get_parent(parent);
        bool parent_is_left = (get_left(grandparent) == parent);
        bool curr_is_left = (get_left(parent) == curr);
        HeapWord* uncle = parent_is_left? get_right(grandparent) : get_left(grandparent);

        // Case 1: Uncle is red
        if (is_red(uncle)) {
            //log_trace(gc_testing)("Case 1: Uncle is red");
            set_black(parent);
            set_black(uncle);
            set_red(grandparent);
        }
        // Case 2: Uncle is black
        else {
            //log_trace(gc_testing)("Case 2: Uncle is black");
            
            // Left 
            if (parent_is_left) {

                // Right
                if (!curr_is_left) {
                    // Case 2a: Triangle (Left-Right)
                    //log_trace(gc_testing)("Case 2a: Triangle (Left-Right)");
                    curr = parent;
                    left_rotate(root, curr);
                    // After rotating we are in same case as:
                }
                // Case 2b: Line (Left-Left)
                //log_trace(gc_testing)("Case 2b: Line (Left-Left)");

                parent = get_parent(curr);
                grandparent = get_parent(parent);

                set_black(parent);
                set_red(grandparent);

                right_rotate(root, grandparent);
            } 
            // Right
            else {

                // Left
                if (curr_is_left) {
                    // Case 2c: Triangle (Right-Left)
                    //log_trace(gc_testing)("Case 2c: Triangle (Right-Left)");
                    curr = parent;
                    right_rotate(root, curr);
                    // After rotating we are in same case as:
                }
                // Case 2d: Line. Right-Right
                //log_trace(gc_testing)("Case 2d: Line (Right-Right)");
                
                parent = get_parent(curr);
                grandparent = get_parent(parent);

                set_black(parent);
                set_red(grandparent);

                left_rotate(root, grandparent);
            }

        }
        
        curr = grandparent;
    }

    set_black(root);
}

void G1RegionFreeSpaceTracker::rb_remove_fixup(HeapWord* &root, HeapWord* x, HeapWord* x_parent) {

    HeapWord* w = nullptr;

    while (x != root && is_black(x)) {

        if (x == get_left(x_parent)) {

            w = get_right(x_parent);

            if (is_red(w)) {
                set_black(w);
                set_red(x_parent);
                left_rotate(root, x_parent);
                w = get_right(x_parent);
            }

            if (is_black(get_left(w)) && is_black(get_right(w))) {
                set_red(w);
                x = x_parent;
                x_parent  = get_parent(x_parent);
            } else {

                if (is_black(get_right(w))) {
                    set_black(get_left(w));
                    set_red(w);
                    right_rotate(root, w);
                    w = get_right(x_parent);
                }

                set_color(w, get_color(x_parent));
                set_black(x_parent);
                set_black(get_right(w));
                left_rotate(root, x_parent);
                x = root;
            }
        } else {

            w = get_left(x_parent);

            if (is_red(w)) {
                set_black(w);
                set_red(x_parent);
                right_rotate(root, x_parent);
                w = get_left(x_parent);
            }

            if (is_black(get_left(w)) && is_black(get_right(w))) {
                set_red(w);
                x = x_parent;
                x_parent  = get_parent(x_parent);
            } else {

                if (is_black(get_left(w))) {
                    set_black(get_right(w));
                    set_red(w);
                    left_rotate(root, w);
                    w = get_left(x_parent);
                }

                set_color(w, get_color(x_parent));
                set_black(x_parent);
                set_black(get_left(w));
                right_rotate(root, x_parent);
                x = root;
            }
        }
    }

    set_black(x);
}

/*
          z            z          z             z
         /            /            \             \
        x            y              x             y
       / \    =>    / \     OR     / \     =>    / \
          y        x   b              y         x   b
         / \      / \                / \       / \
        a   b        a              a   b         a

*/
void G1RegionFreeSpaceTracker::left_rotate(HeapWord* &root, HeapWord* x) {

    HeapWord* y = get_right(x);
    HeapWord* z = get_parent(x);
    HeapWord* a = get_left(y);

    set_right(x, a);
    if (a != nullptr) {
        set_parent(a, x);
    }

    set_parent(y, z);

    if (x == root) {
        root = y;
    } else if (x == get_left(z)) {
        set_left(z, y);
    } else {
        set_right(z, y);
    }

    set_left(y, x);
    set_parent(x, y);
}

/*
          z            z          z             z
         /            /            \             \
        x            y              x             y
       / \    =>    / \     OR     / \     =>    / \
      y            a   x          y             a   x
     / \              / \        / \               / \
    a   b            b          a   b             b

*/
void G1RegionFreeSpaceTracker::right_rotate(HeapWord* &root, HeapWord* x) {

    HeapWord* y = get_left(x);
    HeapWord* z = get_parent(x);
    HeapWord* b = get_right(y);

    set_left(x, b);
    if (b != nullptr) {
        set_parent(b, x);
    }

    set_parent(y, z);

    if (x == root) {
        root = y;
    } else if (x == get_left(z)) {
        set_left(z, y);
    } else {
        set_right(z, y);
    }

    set_right(y, x);
    set_parent(x, y);
}

HeapWord* G1RegionFreeSpaceTracker::remove_hole_tree(HeapWord* &root, HeapWord* remove) {

    if (remove == nullptr) {
        return nullptr;
    }

    // Remove hole with standard BST 
     HeapWord* replace = nullptr;
     HeapWord* replace_parent = get_parent(remove);
     bool original_color = get_color(remove);

    // No left child (handles also no children)
    if (get_left(remove) == nullptr) {
        replace = get_right(remove);
        transplant(root, remove, replace);
    } 
    // No right child
    else if (get_right(remove) == nullptr) {
        replace = get_left(remove);
        transplant(root, remove, replace);
    } 
    // 2 children
    else {
        HeapWord* successor = get_right(remove);

        while (get_left(successor) != nullptr) {
            successor = get_left(successor);
        }

        original_color = get_color(successor);
        replace = get_right(successor);

        if (get_parent(successor) != remove) {
            transplant(root, successor, get_right(successor));
            set_right(successor, get_right(remove));
            set_parent(get_right(remove), successor);
            replace_parent = get_parent(successor);
        } else {
            replace_parent = successor;
        } 

        transplant(root, remove, successor);
        set_left(successor, get_left(remove));
        set_parent(get_left(remove), successor);
        set_color(successor, get_color(remove));
    }

    // Fix RB properties if original color was black
    if (!original_color) {
        rb_remove_fixup(root, replace, replace_parent);
    }

    return remove;
}



HeapWord* G1RegionFreeSpaceTracker::find_exact_hole(HeapWord* &root, size_t size) const {
    HeapWord* curr = root;

    while (curr != nullptr) {
        if (get_size(curr) == size) return curr;

        if (size <= get_size(curr)) curr = get_left(curr);
        else curr = get_right(curr);
    }
    return nullptr;
}

HeapWord* G1RegionFreeSpaceTracker::find_best_fitting_hole(HeapWord* &root, size_t min_size) const {
    HeapWord* curr = root;

    HeapWord* best_fitting_hole = nullptr;
    while (curr != nullptr) {

        size_t hole_size = get_size(curr);
        
        if (hole_size == min_size) {
            return curr;
        }

        // If current hole size is smaller than min_size continue on right subtree
        if (hole_size < min_size) {
            curr = get_right(curr);
        } 
        // If current hole size is greater than min_size, update best_fitting_hole
        // and continue on left subtree
        else {
            G1HeapRegion* region = _g1h->heap_region_containing(curr);
            if (!region->in_collection_set() && is_splittable(hole_size, min_size)) {
                best_fitting_hole = curr;
            } 
            curr = get_left(curr);
        }
    }

    return best_fitting_hole;
}

bool G1RegionFreeSpaceTracker::is_splittable(size_t min_size, size_t hole_size) const { 
    size_t diff = hole_size - min_size;   
    return diff == 0 || diff >= CollectedHeap::min_fill_size();
}

HeapWord* G1RegionFreeSpaceTracker::find_hole_young(size_t min_word_size,
                                                    size_t desired_word_size,
                                                    size_t* actual_word_size) {

    log_trace(gc_testing)("Find hole (YOUNG) of size %lu", desired_word_size);

    if (_use_tree) {
        HeapWord* hole = find_best_fitting_hole(_root_young, desired_word_size);

        if (hole != nullptr) {
            log_trace(gc_testing)("Found hole for desired_word_size : %ld < %ld", desired_word_size, get_size(hole));
            return split_hole(hole, desired_word_size, actual_word_size, false);
        }

        hole = find_best_fitting_hole(_root_young, min_word_size);

        if (hole != nullptr) {
            log_trace(gc_testing)("Found hole for min_word_size : %ld < %ld", min_word_size, get_size(hole));
            return split_hole(hole, min_word_size, actual_word_size, false);
        }

    } else {

        //TODO: clean up and integrate split_hole method 

        for (uint i = 0; i < _size; i++) {
            HeapWord* obj = _holes_young[i]; // only looks for the first hole, but currently we only store one anyway.
            if (obj != nullptr) {
                size_t available = get_size(obj);
                size_t want_to_allocate = MIN2(available, desired_word_size);
                size_t remaining = available - want_to_allocate;  

                bool no_space_for_filler = (remaining != 0) && (remaining < CollectedHeap::min_fill_size());
                // Skip allocation from this hole if no filler. (Since there is only one survivor hole
                // retrying with next hole does not make sense as there is none).
                if (no_space_for_filler) {
                    continue;
                }

                HeapWord* next = get_next(obj);
                HeapWord* dummy = obj + want_to_allocate;

                log_trace(gc_testing)("\tHole %d: %ld", i, available);

                if (want_to_allocate >= min_word_size) {
                    G1HeapRegion* obj_region = _g1h->heap_region_containing(obj);

                    log_trace(gc_testing)("Hole index : %d", i);             

                    HeapWord* dummy = obj + want_to_allocate;
                    if (remaining == 0) {
                        obj_region->fill_with_dummy_object(obj, want_to_allocate);
                        _holes_young[i] = next == obj_region->end() ? nullptr : next;
                    } else {
                        HeapWord* dummy = obj + want_to_allocate;
                        obj_region->fill_with_dummy_object(obj, want_to_allocate);
                        obj_region->fill_with_dummy_object(dummy, remaining);
                        if (remaining >= _min_hole_size_young) {
                        set_size(dummy, remaining);
                        set_next(dummy, next);
                        _holes_young[i] = dummy;
                        } else {
                        _holes_young[i] = next;
                        }
                    }                

                    *actual_word_size = want_to_allocate;

                    return obj;
                }
            }
        }
    }

    return nullptr;
}

HeapWord* G1RegionFreeSpaceTracker::find_hole_old(size_t min_word_size,
                                                  size_t desired_word_size,
                                                  size_t* actual_word_size) {

    log_trace(gc_testing)("Find hole (OLD) of size %lu (%lu)", desired_word_size, min_word_size);
    //return nullptr;

    if (_use_tree) {

        HeapWord* hole = find_best_fitting_hole(_root_old, desired_word_size);

        if (hole != nullptr) {
            log_trace(gc_testing)("Found hole for desired_word_size : %ld < %ld", desired_word_size, get_size(hole));
            return split_hole(hole, desired_word_size, actual_word_size, false);
        }

        hole = find_best_fitting_hole(_root_old, min_word_size);

        if (hole != nullptr) {
            log_trace(gc_testing)("Found hole for min_word_size : %ld < %ld", min_word_size, get_size(hole));
            return split_hole(hole, min_word_size, actual_word_size, false);
        }
    } else {

        //TODO: clean up and integrate split_hole method 

        for (uint i = 0; i < _size; i++) {
            HeapWord* obj = _holes_old[i];
            if (obj != nullptr) {

                G1HeapRegion* region = _g1h->heap_region_containing(obj);
                if (region->in_collection_set()) { // do not use holes for collection set region
                    continue;
                }
                HeapWord* next = nullptr;
                int j = 0;

                do {

                    size_t available = get_size(obj);
                    size_t want_to_allocate = MIN2(available, desired_word_size);
                    size_t remaining = available - want_to_allocate;

                    next = get_next(obj);
                    HeapWord* dummy = obj + want_to_allocate;
                    if (remaining != 0 && remaining < CollectedHeap::min_fill_size()) {
                    // Update head, dropping the hole - unfortunately we need to drop the whole
                    // hole because of this because the code is not able to unlink within the linked list of holes
                    _holes_old[i] = next == _g1h->heap_region_containing(obj)->end() ? nullptr : next;
                    continue; // That will do nothing currently because of the && false below
                    }

                    log_trace(gc_testing)("\tHole %d (%d): %lu", i, j, available);

                    if (want_to_allocate >= min_word_size) {
                        log_trace(gc_testing)("Hole index : %d", i);              
                        G1HeapRegion* obj_region = _g1h->heap_region_containing(obj);
                        
                        if (remaining == 0) {
                        obj_region->fill_with_dummy_object(obj, want_to_allocate);
                        _holes_old[i] = next == obj_region->end() ? nullptr : next;
                        } else {
                        HeapWord* dummy = obj + want_to_allocate;
                        obj_region->fill_with_dummy_object(obj, want_to_allocate);
                        obj_region->fill_with_dummy_object(dummy, remaining);
                        if (remaining >= _min_hole_size_old) {
                            set_size(dummy, remaining);
                            set_next(dummy, next);
                            _holes_old[i] = dummy;
                        } else {
                            _holes_old[i] = next == obj_region->end() ? nullptr : next;
                        }
                        }
                        log_trace(gc_testing)("obj   (" PTR_FORMAT ")", p2i(obj));
                        log_trace(gc_testing)("dummy (" PTR_FORMAT ")", p2i(dummy));
                        log_trace(gc_testing)("top: %lu, end: %lu, diff: %lu, size: %lu, rem: %lu",
                        p2i(obj_region->top()), p2i(obj_region->end()), pointer_delta(obj_region->end(), obj_region->top()), want_to_allocate, remaining);

                        *actual_word_size = want_to_allocate;
                        log_trace(gc_testing)("Hole returned of size %lu (remaining : %lu)", want_to_allocate, remaining);
                    
                        return obj;
                    }
                    j++;
                    obj = next;
                } while (next != region->end());
            }
        }

    }    

    return nullptr;
}

HeapWord* G1RegionFreeSpaceTracker::split_hole(HeapWord* hole, size_t word_size, size_t* actual_word_size, bool young_gen) {

    if (_use_tree) {        
        log_trace(gc_testing)("Remove hole from tree");
        remove_hole_tree(young_gen? _root_young : _root_old, hole);
    } else {
        //TODO: remove from list
    }

    size_t available = get_size(hole);
    size_t want_to_allocate = word_size;
    size_t remaining = available - want_to_allocate;
    
    G1HeapRegion* region = _g1h->heap_region_containing(hole);

    // Fill hole with dummy object
    region->fill_with_dummy_object(hole, want_to_allocate);

    // If the hole is a gap between top and end, then move the top
    if (region->top() == hole) {
        region->set_top(region->top() + want_to_allocate);
        log_trace(gc_testing)("Top must be updated");
    }         
    // If not, the hole is on the left-hand side of top.
    // Therefore, if there is any remaining rest, it must be filled
    // (guaranteed to be fillable during the selection process)
    if (remaining != 0) {
        HeapWord* dummy = hole + want_to_allocate;
        region->fill_with_dummy_object(dummy, remaining);

        if (young_gen) {
            add_potential_survivor_hole(region, dummy, remaining);
        } else {
            log_trace(gc_testing)("Add rest : %ld", remaining);
            add_potential_old_hole(region, dummy, remaining);
        }            
    }

    *actual_word_size = want_to_allocate;   
    
    // add hole to root region
    _g1h->concurrent_mark()->add_root_region_range(hole, hole + want_to_allocate);

    return hole;

}


void G1RegionFreeSpaceTracker::clean_up_holes_young() {
    log_trace(gc_testing)("Clean up holes young");
    for (uint i = 0; i < _size; i++) {
        _holes_young[i] = nullptr;
    }
    if (_use_tree) {        
        _root_young = nullptr;
    }
}

void G1RegionFreeSpaceTracker::clean_up_holes_old() {
    log_trace(gc_testing)("Clean up holes old");
    for (uint i = 0; i < _size; i++) {
        _holes_old[i] = nullptr;
    }
    if (_use_tree) {
        _root_old = nullptr;
    } 
}

void G1RegionFreeSpaceTracker::remove_region(G1HeapRegion* region) {
    if (_use_tree) {
        
        bool young_gen = region->is_young();

        HeapWord* word = young_gen? _holes_young[region->hrm_index()] : _holes_old[region->hrm_index()];

        while (word != nullptr) {
            remove_hole_tree(young_gen? _root_young : _root_old, word);
            word = get_next(word);
        }

    } 
    _holes_young[region->hrm_index()] = nullptr;
    _holes_old[region->hrm_index()] = nullptr;
}

void G1RegionFreeSpaceTracker::dump_holes() {

    if (_holes_young == nullptr) return;

    log_trace(gc_testing)("Print holes");
    for (uint i = 0; i < _size; i++) {
        HeapWord* word = _holes_young[i];
        if (word != nullptr) {
            G1HeapRegion* region = _g1h->heap_region_containing(word);
            HeapWord* end = region->end();
            log_trace(gc_testing)("Region %u", i);
            while (word != end) {
                size_t size = get_size(word);
                HeapWord* next = end; //getNext(word);
                log_trace(gc_testing)("\tHole at: " PTR_FORMAT ", size = %7lu, next = " PTR_FORMAT,
                    p2i(word), size, p2i(next));
                //word = next;
                word = end;
            }
        }
    }

}

void G1RegionFreeSpaceTracker::dump_regions() {

    uint countEmpty = 0;
    uint count = 0;

    if (_holes_young == nullptr) return;

    log_trace(gc_testing)("Print regions");
    for (uint i = 0; i < _g1h->max_num_regions(); i++) {
        G1HeapRegion* region = _g1h->region_at(i);
        if (region != nullptr) {
            if (region->is_empty()) countEmpty++;
            if (region->is_eden()) log_trace(gc_testing)          ("Region %4u is      eden (bottom|top|end|unused: " PTR_FORMAT " | " PTR_FORMAT " | "  PTR_FORMAT " | %lu)",
     i, p2i(region->bottom()), p2i(region->top()), p2i(region->end()), region->free());
            else if(region->is_survivor()) log_trace(gc_testing)  ("Region %4u is  survivor (bottom|top|end|unused: " PTR_FORMAT " | " PTR_FORMAT " | "  PTR_FORMAT " | %lu)",
     i, p2i(region->bottom()), p2i(region->top()), p2i(region->end()), region->free());
            else if (region->is_old()) log_trace(gc_testing)      ("Region %4u is       old (bottom|top|end|unused: " PTR_FORMAT " | " PTR_FORMAT " | "  PTR_FORMAT " | %lu)",
     i, p2i(region->bottom()), p2i(region->top()), p2i(region->end()), region->free());
            else if (region->is_humongous()) log_trace(gc_testing)("Region %4u is humongous (bottom|top|end|unused: " PTR_FORMAT " | " PTR_FORMAT " | "  PTR_FORMAT " | %lu)",
     i, p2i(region->bottom()), p2i(region->top()), p2i(region->end()), region->free());
            //else if (region->is_free()) log_trace(gc_testing)("Region %u is free", i);
            count++;
        }
    }

    log_trace(gc_testing)("Regions : %u regions (%u empty) of %u max", count, countEmpty, _g1h->max_num_regions());
}


void G1RegionFreeSpaceTracker::dump_hole_stats() {

    log_trace(gc_stats)("Before GC:");

    uint usedRegions = 0;
    size_t size = 0;

    for (uint i = 0; i < _g1h->max_num_regions(); i++) {
        G1HeapRegion* region = _g1h->region_at(i);
        if (region == nullptr) continue;
        if (region->is_empty() || region-> is_eden()) continue;

        HeapWord* current = region->bottom();

        usedRegions++;
        size = region->capacity();

        if (region->is_humongous()) {
            if (region->free() == 0) continue;
            else current = region->top();
        }
        
        log_trace(gc_stats)("Region %u (%s)", i, region->get_type_str());

        while (current < region->top()) {

            oop obj = cast_to_oop(current);

            if (_g1h->is_obj_filler(obj)) {
                log_trace(gc_stats)("Hole size = %7lu from " PTR_FORMAT " to " PTR_FORMAT " (filler)", 
                    obj->size(), p2i(current), p2i(current + obj->size()));
            } /*else {
                log_trace(gc_stats)("Obj size = %7lu from " PTR_FORMAT " to " PTR_FORMAT " (filler)", 
                    obj->size(), p2i(current), p2i(current + obj->size()));
            }*/

            current += obj->size();
        }

        if (region->free() != 0) {            
            log_trace(gc_stats)("Hole size = %7lu from " PTR_FORMAT " to " PTR_FORMAT " (end)", 
                region->free(), p2i(region->top()), p2i(region->end()));
        }

    }


    log_trace(gc_stats)("Used Regions: %u", usedRegions);
    log_trace(gc_stats)("Size: %lu", size);

}

void G1RegionFreeSpaceTracker::dump_tree_node(HeapWord* word) {
    if (word == nullptr) return;

    HeapWord* parent = get_parent(word);
    HeapWord* left = get_left(word);
    HeapWord* right = get_right(word);
    bool red = is_red(word);

    log_trace(gc_testing)("---------------------------------");
    log_trace(gc_testing)("Word   : " PTR_FORMAT, p2i(word));
    log_trace(gc_testing)("Size   : %ld", get_size(word));
    log_trace(gc_testing)("Parent : " PTR_FORMAT " (%ld)", p2i(parent), parent == nullptr? -1 : get_size(parent));
    log_trace(gc_testing)("Left   : " PTR_FORMAT " (%ld)", p2i(left), left == nullptr? -1 : get_size(left));
    log_trace(gc_testing)("Right  : " PTR_FORMAT " (%ld)", p2i(right), right == nullptr? -1 : get_size(right));
    log_trace(gc_testing)("Color  : %s", red? "red" : "black");
    log_trace(gc_testing)("---------------------------------");
}

void G1RegionFreeSpaceTracker::inorder_traversal(HeapWord* root) {

    if (root == nullptr) {
        log_trace(gc_testing)("Node : NULL");
        return;
    }

    inorder_traversal(get_left(root));
    dump_tree_node(root);
    inorder_traversal(get_right(root));

}