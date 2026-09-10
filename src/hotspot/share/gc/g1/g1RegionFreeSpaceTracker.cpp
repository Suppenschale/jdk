
#include "gc/g1/g1AllocRegion.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1ConcurrentMarkThread.inline.hpp"
#include "gc/g1/g1RegionFreeSpaceTracker.hpp"
#include "logging/log.hpp"
#include "utilities/hashTable.hpp"
#include "utilities/resizableHashTable.hpp"


G1RegionFreeSpaceTracker::G1RegionFreeSpaceTracker(G1CollectedHeap* heap) :
    _g1h(heap),
    _holes_young(nullptr),
    _holes_old(nullptr),
    _root_young(nullptr),
    _root_old(nullptr),
    _size(0),
    _min_hole_size_young(0),
    _min_hole_size_old(0)
{
    log_trace(gc_testing)("Init G1RegionFreeSpaceTracker");


    log_trace(gc_testing)("Size of ListHole: %ld", sizeof(ListHole));
    log_trace(gc_testing)("Size of TreeHole: %ld", sizeof(TreeHole));

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

    if (use_tree()) {
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

bool G1RegionFreeSpaceTracker::use_list() const {
    return G1HoleDataStructure == 0;
}

bool G1RegionFreeSpaceTracker::use_bst() const {
    return G1HoleDataStructure == 1;
}

bool G1RegionFreeSpaceTracker::use_rbt() const {
    return G1HoleDataStructure == 2;
}

bool G1RegionFreeSpaceTracker::use_tree() const {
    return G1HoleDataStructure > 0;
}

bool G1RegionFreeSpaceTracker::add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
    
    /*bool created;
    size_t* count = _hole_statistics_young.put_if_absent(size_in_words, &created);
    (*count)++;*/

    if (size_in_words < _min_hole_size_young) {
        return false;
    }
    
    add_hole_list(_holes_young, region, word, size_in_words);
    if (use_tree()) {        
        add_hole_tree(_root_young, word, size_in_words);
    } 
    return true;
}

bool G1RegionFreeSpaceTracker::add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {   
    
    /*bool created;
    size_t* count = _hole_statistics_humongous.put_if_absent(size_in_words, &created);
    (*count)++;*/

    return add_potential_old_hole(region, word, size_in_words);
}

bool G1RegionFreeSpaceTracker::add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {       
    if (region->has_pinned_objects()) { // Reject pinned regions: they may contain valid objects anywhere (actually it would be possible, but then would need to reject at allocation side. This is more complicated.)
        return false;
    }
    if (size_in_words < _min_hole_size_old) {
        return false;
    }

    add_hole_list(_holes_old, region, word, size_in_words);
    if (use_tree()) {
        add_hole_tree(_root_old, word, size_in_words);
    } 
    return true;
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
    if (word == nullptr ) return nullptr;
    return get_hole_tree(word)->parent;
}

void G1RegionFreeSpaceTracker::set_left(HeapWord* word, HeapWord* left) {
    TreeHole* hole = get_hole_tree(word);
    hole->left = left;
}

HeapWord* G1RegionFreeSpaceTracker::get_left(HeapWord* word) const {
    if (word == nullptr ) return nullptr;
    return get_hole_tree(word)->left;
}

void G1RegionFreeSpaceTracker::set_right(HeapWord* word, HeapWord* right) {
    TreeHole* hole = get_hole_tree(word);
    hole->right = right;
}

HeapWord* G1RegionFreeSpaceTracker::get_right(HeapWord* word) const {
    if (word == nullptr ) return nullptr;
    return get_hole_tree(word)->right;
}

void G1RegionFreeSpaceTracker::set_color(HeapWord* word, bool red) {
    if (word == nullptr) return;
    TreeHole* hole = get_hole_tree(word);
    hole->red = red;
}

bool G1RegionFreeSpaceTracker::get_color(HeapWord* word) const {
    if (word == nullptr) return false;
    return get_hole_tree(word)->red; 
}

void G1RegionFreeSpaceTracker::set_red(HeapWord* word) {
    set_color(word, true);
}

bool G1RegionFreeSpaceTracker::is_red(HeapWord* word) const {
    if (word == nullptr) return false;
    return get_color(word);
}

void G1RegionFreeSpaceTracker::set_black(HeapWord* word) {
    set_color(word, false);
}

bool G1RegionFreeSpaceTracker::is_black(HeapWord* word) const {
    if (word == nullptr) return false;
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
    if (use_rbt() && is_red(get_parent(word))) {
        rb_insert_fixup(root, word);
    }
}

void G1RegionFreeSpaceTracker::transplant(HeapWord* &root, HeapWord* u, HeapWord* v) {
    assert(u != nullptr, "u must not be null");

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

    if (root == nullptr || word == nullptr) return;

    HeapWord* curr = word;

    while (is_red(get_parent(curr))) {
        
        HeapWord* parent = get_parent(curr);
        HeapWord* grandparent = get_parent(parent);

        if (grandparent == nullptr) {
            log_error(gc_testing)("grandparent is null in insert fixup");
            break;
        }

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

    if (root == nullptr) return;

    HeapWord* w = nullptr;

    while (x != root && is_black(x)) {

        if (x_parent == nullptr && x != root) {
            log_error(gc_testing)("x_parent is null in remove fixup");
            break;
        }

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

    if (x == nullptr) {
        log_error(gc_testing)("left_rotate called with null x");
        return;
    }

    HeapWord* y = get_right(x);

    if (y == nullptr) {
        log_error(gc_testing)("left_rotate called on node without right child");
        log_error(gc_testing)("x=" PTR_FORMAT ", size=%ld, parent=" PTR_FORMAT, 
                                p2i(x), get_size(x), p2i(get_parent(x)));
    return;
    }

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

    if (x == nullptr) {
        log_error(gc_testing)("right_rotate called with null x");
        return;
    }

    HeapWord* y = get_left(x);

    if (y == nullptr) {
        log_error(gc_testing)("right_rotate called on node without left child");
        log_error(gc_testing)("x=" PTR_FORMAT ", size=%ld, parent=" PTR_FORMAT, 
                                p2i(x), get_size(x), p2i(get_parent(x)));
        return;
    }

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

HeapWord* G1RegionFreeSpaceTracker::remove_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* remove) {

    HeapWord* start = list[region->hrm_index()];
    HeapWord* curr = start;
    HeapWord* prev = start;

    while (curr != nullptr) {
        if (curr == remove) {
            set_next(prev, get_next(curr));
            set_next(curr, nullptr);

            if (curr == start) {
                list[region->hrm_index()] = get_next(curr);
            }

            break;
        }
        prev = curr;
        curr = get_next(curr);
    }

    return curr;
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
    if (use_rbt() && !original_color) {
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

HeapWord* G1RegionFreeSpaceTracker::find_first_fitting_hole(HeapWord** list, size_t min_size) const {

    for (uint i = 0; i < _size; i++) {

        HeapWord* curr = list[i]; 

        if (curr == nullptr) {
            continue;
        }
        G1HeapRegion* r = _g1h->heap_region_containing(curr);
        // Do not allow allocation into pinned regions.
        // Also continue if region is part of collection set (and not young) 
        bool check = r->has_pinned_objects() || (r->in_collection_set() && !r->is_young());
        if (r->has_pinned_objects()) { 
            continue;
        }

        while (curr != nullptr) {

            size_t hole_size = get_size(curr);

            if (hole_size >= min_size && is_splittable(min_size, hole_size)) {
                return curr;
            } 

            curr = get_next(curr);
        }
    }

    return nullptr;
}


HeapWord* G1RegionFreeSpaceTracker::find_best_fitting_hole(HeapWord* &root, size_t min_size) const {
    HeapWord* curr = root;

    HeapWord* best_fitting_hole = nullptr;
    uint depth = 0;
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
            bool check = !region->has_pinned_objects() && 
                         (region->is_young() || !region->in_collection_set()) && 
                         is_splittable(min_size, hole_size);
            if (check) {
                best_fitting_hole = curr;
            } 
            curr = get_left(curr);
        }
        depth++;
        if (use_bst() && depth > MAX_DEPTH) {
            return best_fitting_hole;
        }
    }

    return best_fitting_hole;
}

bool G1RegionFreeSpaceTracker::is_splittable(size_t min_size, size_t hole_size) const {
    assert(hole_size >= min_size, "hole must be larger than min size");
    size_t diff = hole_size - min_size;  
    return diff == 0 || diff >= CollectedHeap::min_fill_size();
}


HeapWord* G1RegionFreeSpaceTracker::find_hole(size_t min_word_size,
                                              size_t desired_word_size,
                                              size_t* actual_word_size,
                                              bool young_gen) {

    if (young_gen) {
        all_young++;
        hit_young++;
    } else {
        all_old++;
        hit_old++;
    }

    if (use_tree()) {

        HeapWord* hole = find_best_fitting_hole(young_gen? _root_young : _root_old, desired_word_size);

        if (hole != nullptr) {
            return split_hole(hole, desired_word_size, actual_word_size, young_gen);
        }

        hole = find_best_fitting_hole(young_gen? _root_young : _root_old, min_word_size);

        if (hole != nullptr) {
            return split_hole(hole, min_word_size, actual_word_size, young_gen);
        }

    } else {
        HeapWord** root = young_gen ? _holes_young : _holes_old;
        HeapWord* hole = find_first_fitting_hole(root, desired_word_size);
        if (hole != nullptr) {
            return split_hole(hole, desired_word_size, actual_word_size, young_gen);
        }

        hole = find_first_fitting_hole(root, min_word_size);
        if (hole != nullptr) {
            return split_hole(hole, min_word_size, actual_word_size, young_gen);
        }
    }

    if (young_gen) {
        hit_young--;
    } else {
        hit_old--;
    }
    return nullptr;
}


HeapWord* G1RegionFreeSpaceTracker::find_young_hole(size_t min_word_size,
                                                    size_t desired_word_size,
                                                    size_t* actual_word_size) {
    assert(!SafepointSynchronize::is_at_safepoint(), "do not reuse survivor holes at safepoint");
    return find_hole(min_word_size, desired_word_size, actual_word_size, true);
}

HeapWord* G1RegionFreeSpaceTracker::find_old_hole(size_t min_word_size,
                                                  size_t desired_word_size,
                                                  size_t* actual_word_size) {
    assert(SafepointSynchronize::is_at_safepoint(), "do not reuse old holes outside safepoint");
    return find_hole(min_word_size, desired_word_size, actual_word_size, false);
}

HeapWord* G1RegionFreeSpaceTracker::split_hole(HeapWord* hole, size_t word_size, size_t* actual_word_size, bool young_gen) {

    G1HeapRegion* region = _g1h->heap_region_containing(hole);

    remove_hole_list(young_gen? _holes_young : _holes_old, region, hole);
    if (use_tree()) {        
        remove_hole_tree(young_gen? _root_young : _root_old, hole);
    } 

    size_t available = get_size(hole);
    size_t want_to_allocate = word_size;
    size_t remaining = available - want_to_allocate;    

    // Fill hole with dummy object; since we might be operating on a humongous tail region, we need to force BOT update.
    region->fill_with_dummy_object(hole, want_to_allocate, false /* zap */, true /* force */);

    // If the hole is a gap between top and end, then move the top
    if (region->top() == hole) {
        region->set_top(region->top() + want_to_allocate);
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
            add_potential_old_hole(region, dummy, remaining);
        }            
    }

    *actual_word_size = want_to_allocate;   
    
    // add hole to root region
    // should only be called during GC, for old gen/humongous holes.
    if (!young_gen) {
      G1ConcurrentMark* cm = _g1h->concurrent_mark();
      MemRegion mr(hole, hole + want_to_allocate);
      if (_g1h->collector_state()->in_concurrent_start_gc()) {
        cm->add_root_region_range(mr);
      }
      if (cm->cm_thread()->in_progress()) {
        cm->add_to_allocation_tree(mr);  
      }
    }

    

    return hole;
}


void G1RegionFreeSpaceTracker::clean_up_young_holes() {
    for (uint i = 0; i < _size; i++) {
        _holes_young[i] = nullptr;
    }
    if (use_tree()) {        
        _root_young = nullptr;
    }
}

void G1RegionFreeSpaceTracker::clean_up_old_holes() {
    for (uint i = 0; i < _size; i++) {
        _holes_old[i] = nullptr;
    }
    if (use_tree()) {
        _root_old = nullptr;
    } 
}

void G1RegionFreeSpaceTracker::remove_region(G1HeapRegion* region) {
    if (use_tree()) {
        
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


void G1RegionFreeSpaceTracker::increment_young_min_statistic(size_t size_in_words) {
    bool created;
    size_t* count = _object_min_statistics_young.put_if_absent(size_in_words, &created);
    (*count)++;
}

void G1RegionFreeSpaceTracker::increment_young_desired_statistic(size_t size_in_words) {
    bool created;
    size_t* count = _object_desired_statistics_young.put_if_absent(size_in_words, &created);
    (*count)++;
}

void G1RegionFreeSpaceTracker::increment_old_min_statistic(size_t size_in_words) {
    bool created;
    size_t* count = _object_min_statistics_old.put_if_absent(size_in_words, &created);
    (*count)++;
}

void G1RegionFreeSpaceTracker::increment_old_desired_statistic(size_t size_in_words) {
    bool created;
    size_t* count = _object_desired_statistics_old.put_if_absent(size_in_words, &created);
    (*count)++;
}

void G1RegionFreeSpaceTracker::print_statistics() const {
/*
    log_trace(gc_testing)("Young Holes Statistics: ");
    _hole_statistics_young.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:young_hole;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });

    log_trace(gc_testing)("Old Holes Statistics: ");
    _hole_statistics_old.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:old_hole;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });

    log_trace(gc_testing)("Humongous Holes Statistics: ");
     _hole_statistics_humongous.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:humongous_hole;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });
*//*
    log_trace(gc_testing)("Young Min Object Statistics: ");
    _object_min_statistics_young.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:young_object_min;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });

    log_trace(gc_testing)("Young Desired Object Statistics: ");
    _object_desired_statistics_young.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:young_object_desired;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });

    log_trace(gc_testing)("Old Min Object Statistics: ");
    _object_min_statistics_old.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:old_object_min;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });

    log_trace(gc_testing)("Old Desired Object Statistics: ");
    _object_desired_statistics_old.iterate_all([](size_t key, size_t value) {
        log_trace(gc_testing)("Collect:old_object_desired;%ld;%ld;%ld", key, value, G1HeapRegion::GrainBytes);
    });*/

    if (all_young > 0) {
        log_trace(gc_testing)("Hit Ratio Young: %.4f Percent(%ld / %ld)", ((double)hit_young) / all_young * 100.0f, hit_young, all_young);
    }
    if (all_old > 0) {
        log_trace(gc_testing)("Hit Ratio Old: %.4f Percent (%ld / %ld)", ((double)hit_old) / all_old * 100.0f, hit_old, all_old);
    }

}