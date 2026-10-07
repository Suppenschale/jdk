
#include "gc/g1/g1AllocRegion.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1ConcurrentMarkThread.inline.hpp"
#include "gc/g1/g1RegionFreeSpaceTracker.hpp"
#include "logging/log.hpp"
#include "utilities/hashTable.hpp"
#include "utilities/resizableHashTable.hpp"

static int filler_header_size() {
  return MAX2((int)CollectedHeap::min_fill_size(), align_up(arrayOopDesc::header_size_in_bytes(), HeapWordSize) / HeapWordSize);
}

G1RegionFreeSpaceTracker::G1RegionFreeSpaceTracker(G1CollectedHeap* heap) :
    _g1h(heap),
    _holes_young(nullptr),
    _holes_old(nullptr),
    _root_young(nullptr),
    _root_old(nullptr),
    _size(0),
    _min_hole_size_young_in_words(0),
    _min_hole_size_old_in_words(0),
    _hit_young(0), _all_young(0), _hit_old(0), _all_old(0)
{
    log_trace(gc_testing)("Init G1RegionFreeSpaceTracker");


    log_trace(gc_testing)("Size of ListHole: %ld", sizeof(ListHole));
    log_trace(gc_testing)("Size of TreeHole: %ld", sizeof(TreeHole));



    log_trace(gc_testing)("CollectedHeap::filler_array_min_size() = %ld", _g1h->filler_array_min_size());
    log_trace(gc_testing)("CollectedHeap::min_dummy_object_size() = %ld", CollectedHeap::min_dummy_object_size());
    log_trace(gc_testing)("CollectedHeap::min_fill_size()         = %ld", CollectedHeap::min_fill_size());
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
        _min_hole_size_young_in_words = filler_header_size() + words_for_struct;
        _min_hole_size_old_in_words = G1CardTable::card_size_in_words(); // Must be at least a card because need card alignment.
    } else {
        // Use list structure
        size_t words_for_struct = (sizeof(ListHole) + sizeof(HeapWord) - 1) / sizeof(HeapWord);
        _min_hole_size_young_in_words = filler_header_size() + words_for_struct;
        _min_hole_size_old_in_words = G1CardTable::card_size_in_words(); // Must be at least a card because need card alignment.
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


bool G1RegionFreeSpaceTracker::hole_in_list(HeapWord** list, G1HeapRegion* region, HeapWord* word) {

    HeapWord* curr = list[region->hrm_index()];

    /*g_trace(gc_testing)("Check word in list: " PTR_FORMAT, p2i(word));

    
    parse_tree(_root_old);
    parse_list(_holes_old);
    if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");*/
    while (curr != nullptr) {
        //log_trace(gc_testing)("hole_in_list");
        if (curr == word) return true;
        curr = get_next(curr);
    }
    return false;
}


bool G1RegionFreeSpaceTracker::add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
    
    /*bool created;
    size_t* count = _hole_statistics_young.put_if_absent(size_in_words, &created);
    (*count)++;*/

    if (size_in_words < _min_hole_size_young_in_words) {
        return false;
    }

    if (hole_in_list(_holes_young, region, word)) {
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

bool G1RegionFreeSpaceTracker::add_potential_old_hole(G1HeapRegion* region, HeapWord* start, size_t size_in_words) {
    if (region->has_pinned_objects()) { // Reject pinned regions: they may contain valid objects anywhere (actually it would be possible, but then would need to reject at allocation side. This is more complicated.)
        return false;
    }
    if (size_in_words < _min_hole_size_old_in_words) {
        return false;
    }

    HeapWord* end = start + size_in_words;

    HeapWord* aligned_start = align_up(start, G1CardTable::card_size());
    HeapWord* aligned_end = align_down(end, G1CardTable::card_size());

    size_t start_fill = pointer_delta(aligned_start, start);
    if (start_fill != 0) {
        if (start_fill < CollectedHeap::min_fill_size()) {
            start_fill += G1CardTable::card_size_in_words();
            aligned_start += G1CardTable::card_size_in_words();
        }
    }
    size_t end_fill = pointer_delta(end, aligned_end);
    if (end_fill != 0) {
        if (end_fill < CollectedHeap::min_fill_size()) {
            end_fill += G1CardTable::card_size_in_words();
            aligned_end -= G1CardTable::card_size_in_words();
        }
    }
    if (aligned_start >= aligned_end) {
        _g1h->fill_with_objects(start, size_in_words);
        region->update_bot_for_block(start, start + size_in_words);
        return false;
    }

    if (hole_in_list(_holes_old, region, aligned_start)) {
        return false;
    }

    if (start_fill != 0) {
        _g1h->fill_with_objects(start, start_fill);
        region->update_bot_for_block(start, start + start_fill);
    }
    if (end_fill != 0) {
        _g1h->fill_with_object(aligned_end, end_fill);
        region->update_bot_for_block(aligned_end, aligned_end + end_fill);
    }
    size_t hole_size_in_words = pointer_delta(aligned_end, aligned_start);
    _g1h->fill_with_objects(aligned_start, hole_size_in_words);
    region->update_bot_for_block(aligned_start, aligned_end);

    add_hole_list(_holes_old, region, aligned_start, hole_size_in_words);
    if (use_tree()) {
        add_hole_tree(_root_old, aligned_start, hole_size_in_words);
    }
    return true;
}


void G1RegionFreeSpaceTracker::set_hole_list(HeapWord* word, size_t size, HeapWord* next) {
    *(ListHole*)(word + filler_header_size()) = ListHole {size, next};
}

ListHole* G1RegionFreeSpaceTracker::get_hole_list(HeapWord* word) const {
    return (ListHole*)(word + filler_header_size());
}

void G1RegionFreeSpaceTracker::set_hole_tree(HeapWord* word, size_t size_in_words) {
    HeapWord* next = get_next(word);
    *(TreeHole*)(word + filler_header_size()) = TreeHole {size_in_words, next, nullptr, nullptr, nullptr, true};
}

TreeHole* G1RegionFreeSpaceTracker::get_hole_tree(HeapWord* word) const {
    return (TreeHole*)(word + filler_header_size());
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
    if (word == nullptr) return;
    TreeHole* hole = get_hole_tree(word);
    hole->parent = parent;
}

HeapWord* G1RegionFreeSpaceTracker::get_parent(HeapWord* word) const {
    if (word == nullptr ) return nullptr;
    return get_hole_tree(word)->parent;
}

void G1RegionFreeSpaceTracker::set_left(HeapWord* word, HeapWord* left) {
    if (word == nullptr) return;

    if ((uintptr_t)left % sizeof(HeapWord) != 0) {
        log_error(gc_testing)("left : " PTR_FORMAT " not aligned?", p2i(left));
    }

    TreeHole* hole = get_hole_tree(word);
    hole->left = left;
}

HeapWord* G1RegionFreeSpaceTracker::get_left(HeapWord* word) const {
    if (word == nullptr ) return nullptr;
    return get_hole_tree(word)->left;
}

void G1RegionFreeSpaceTracker::set_right(HeapWord* word, HeapWord* right) {
    if (word == nullptr) return;

    if ((uintptr_t)right % sizeof(HeapWord) != 0) {
        log_error(gc_testing)("right : " PTR_FORMAT " not aligned?", p2i(right));
    }

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
    return get_color(word);
}

void G1RegionFreeSpaceTracker::set_black(HeapWord* word) {
    set_color(word, false);
}

bool G1RegionFreeSpaceTracker::is_black(HeapWord* word) const {
    return !get_color(word);
}

void G1RegionFreeSpaceTracker::add_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
    HeapWord* next = list[region->hrm_index()];
    list[region->hrm_index()] = word;
    set_hole_list(word, size_in_words, next);
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
        return;
    }

    HeapWord* y = get_right(x);

    if (y == nullptr) {
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
        return;
    }

    HeapWord* y = get_left(x);

    if (y == nullptr) {
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

void G1RegionFreeSpaceTracker::transplant(HeapWord* &root, HeapWord* u, HeapWord* v) {

    HeapWord* u_parent = get_parent(u);
    if (u == root) {
        root = v;
    } else if (u == get_left(u_parent)) {
        set_left(u_parent, v);
    } else {
        set_right(u_parent, v);
    }
    set_parent(v, u_parent);
}

void G1RegionFreeSpaceTracker::add_hole_tree(HeapWord* &root, HeapWord* z, size_t size_in_words) {
    
    set_hole_tree(z, size_in_words);

    HeapWord* y = nullptr;
    HeapWord* x = root;

    while (x != nullptr) {
        //log_trace(gc_testing)("add_hole_tree");
        y = x;
        if (get_size(z) <= get_size(x)) {
            x = get_left(x);
        } else {
            x = get_right(x);
        }     
    }
    set_parent(z, y);
    if (y == nullptr) {
        root = z;
    } else if (get_size(z) <= get_size(y)){
        set_left(y, z);
    } else {
        set_right(y, z);
    }
    set_left(z, nullptr);
    set_right(z, nullptr);
    set_red(z);

    if (use_rbt()) {
        /*
        dump_tree_node(z);
        log_trace(gc_testing)("Before insert fixup");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");*/
        rb_insert_fixup(root, z);
        /*log_trace(gc_testing)("After insert fixup");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");*/
    }
    /*if (use_rbt() && !verify_tree(root)) {
        log_error(gc_testing)("Tree is not valid after insert!");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");
    }*/
}



void G1RegionFreeSpaceTracker::rb_insert_fixup(HeapWord* &root, HeapWord* z) {

    int count = 0;

    while (is_red(get_parent(z))) {
        log_trace(gc_testing)("rb_insert_fixup (%d)", count);
        count++;
        if (100 < count) {
            ShouldNotReachHere();
        }
        
        
        HeapWord* parent = get_parent(z);
        HeapWord* grandparent = get_parent(parent);

        if (parent == get_left(grandparent)) {
            HeapWord* y = get_right(grandparent);
            if (is_red(y)) {
                set_black(parent);
                set_black(y);
                set_red(grandparent);
                z = grandparent;
            } else {
                if (z == get_right(parent)) {
                    z = parent;
                    left_rotate(root, z);
                    parent = get_parent(z);
                    grandparent = get_parent(parent);
                }
                set_black(parent);
                set_red(grandparent);
                right_rotate(root, grandparent);
            }
        } else {
            HeapWord* y = get_left(grandparent);
            if (is_red(y)) {
                set_black(parent);
                set_black(y);
                set_red(grandparent);
                z = grandparent;
            } else {
                if (z == get_left(parent)) {
                    z = parent;
                    right_rotate(root, z);
                    parent = get_parent(z);
                    grandparent = get_parent(parent);
                }
                set_black(parent);
                set_red(grandparent);
                left_rotate(root, grandparent);
            }
        }
    }
    set_black(root);
}

HeapWord* G1RegionFreeSpaceTracker::remove_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* remove) {

    HeapWord*& head = list[region->hrm_index()];

    HeapWord* curr = head;
    HeapWord* prev = nullptr;

    while (curr != nullptr) {
        if (curr == remove) {  
            HeapWord* next = get_next(curr);

            if (prev == nullptr) {
                head = next;
            } else {
                set_next(prev, next);
            }
            set_next(curr, nullptr);
            return curr;
        }
        prev = curr;
        curr = get_next(curr);
    }
    return nullptr;
}

HeapWord* G1RegionFreeSpaceTracker::remove_hole_tree(HeapWord* &root, HeapWord* z) {

    HeapWord* y = z;
    HeapWord* x = nullptr;
    HeapWord* x_parent = nullptr;
    bool y_color = get_color(y);

    if (get_left(z) == nullptr) {
        x = get_right(z);
        x_parent = get_parent(z);
        transplant(root, z, get_right(z));
    } else if (get_right(z) == nullptr) {
        x = get_left(z);
        x_parent = get_parent(z);
        transplant(root, z, get_left(z));
    } else {
        y = tree_minimum(get_right(z));
        y_color = get_color(y);
        x = get_right(y);
        if (get_parent(y) == z) {
            x_parent = y;
            set_parent(x, y);
        } else {
            x_parent = get_parent(y);
            transplant(root, y, get_right(y));
            set_right(y, get_right(z));
            set_parent(get_right(y), y);
        } 
        transplant(root, z, y);
        set_left(y, get_left(z));
        set_parent(get_left(y), y);
        set_color(y, get_color(z));
    }

    if (use_rbt() && !y_color) {
        /*log_trace(gc_testing)("Before delete fixup");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");*/
        rb_delete_fixup(root, x, x_parent);
        /*log_trace(gc_testing)("After delete fixup");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");*/
    }
    /*if (use_rbt() && !verify_tree(root)) {
        log_error(gc_testing)("Tree is not valid after remove!");
        parse_tree(root);
        parse_list(_holes_old);
        if (!check_synchronization(_root_old, _holes_old)) log_error(gc_testing)("Lists and tree are asynchronized");
    }*/

    return z;
}



void G1RegionFreeSpaceTracker::rb_delete_fixup(HeapWord* &root, HeapWord* x, HeapWord* x_parent) {

    int count = 0;

    while (x != root && is_black(x)) {
        //log_trace(gc_testing)("rb_delete_fixup");
        log_trace(gc_testing)("rb_delete_fixup (%d)", count);
        count++;
        if (100 < count) {
            ShouldNotReachHere();
        }

        if (x == get_left(x_parent)) {
            HeapWord* w = get_right(x_parent);
            if (is_red(w)) {
                set_black(w);
                set_red(x_parent);
                left_rotate(root, x_parent);
                w = get_right(x_parent);
            }
            if (is_black(get_left(w)) && is_black(get_right(w))) {
                set_red(w);
                x = x_parent;
                x_parent = get_parent(x);
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
                x_parent = nullptr;
            }
        } else {
            HeapWord* w = get_left(x_parent);
            if (is_red(w)) {
                set_black(w);
                set_red(x_parent);
                right_rotate(root, x_parent);
                w = get_left(x_parent);
            }
            if (is_black(get_left(w)) && is_black(get_right(w))) {
                set_red(w);
                x = x_parent;
                x_parent = get_parent(x);
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
                x_parent = nullptr;
            }
        }
    }
    set_black(x);
}

HeapWord* G1RegionFreeSpaceTracker::tree_minimum(HeapWord* x) {
    while (get_left(x) != nullptr) {
        //log_trace(gc_testing)("tree_minimum");
        x = get_left(x);
    }
    return x;
}


HeapWord* G1RegionFreeSpaceTracker::find_exact_hole(HeapWord* &root, size_t size) const {
    HeapWord* curr = root;

    while (curr != nullptr) {
        //log_trace(gc_testing)("find_exact_hole");
        if (get_size(curr) == size) return curr;

        if (size <= get_size(curr)) curr = get_left(curr);
        else curr = get_right(curr);
    }
    return nullptr;
}

static bool is_eligible_region(uint region_index) {
    G1CollectedHeap* g1h = G1CollectedHeap::heap();
    G1HeapRegion* r = g1h->region_at(region_index);
    if (r->has_pinned_objects()) { // Do not allow allocation into pinned regions. They may still be in use.
        return false;
    }
    // Region attributes for former survivor regions are already marked as in collection set, so skip the next check
    // when outside safepoint (= mutator allocation).
    if (!SafepointSynchronize::is_at_safepoint()) {
      return true;
    }
    G1HeapRegionAttr attr = g1h->region_attr(region_index);
    if (attr.is_in_cset() || attr.is_optional()) {
        return false;
    }
    return true;
}

HeapWord* G1RegionFreeSpaceTracker::find_first_fitting_hole(HeapWord** list, size_t min_size, bool* holes_exhausted) const {

    size_t num_null = 0;
    *holes_exhausted = false;

    uint loop_break = 0;
    for (uint i = 0; i < _size; i++) {

        HeapWord* curr = list[i]; 

        if (curr == nullptr) {
            num_null++;
            continue;
        }
        if (!is_eligible_region(_g1h->addr_to_region(curr))) {
            continue;
        }

        loop_break = 0;
        while (curr != nullptr && loop_break < 100) {
            //log_trace(gc_testing)("find_first_fitting_hole");

            size_t hole_size = get_size(curr);

            if (hole_size >= min_size && is_splittable(min_size, hole_size)) {
                return curr;
            } 

            curr = get_next(curr);
            loop_break++;
        }
    }

    *holes_exhausted = num_null == _size;
    return nullptr;
}


HeapWord* G1RegionFreeSpaceTracker::find_best_fitting_hole(HeapWord* &root, size_t min_size) const {
    HeapWord* curr = root;

    HeapWord* best_fitting_hole = nullptr;
    uint depth = 0;
    while (curr != nullptr) {
        //log_trace(gc_testing)("find_best_fitting_hole");

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
                                              bool young_gen,
                                              size_t* used_change,
                                              bool* holes_exhausted) {

    if (young_gen) {
        _all_young++;
        _hit_young++;
    } else {
        _all_old++;
        _hit_old++;
    }

    if (use_tree()) {

        HeapWord* hole = find_best_fitting_hole(young_gen? _root_young : _root_old, desired_word_size);

        if (hole != nullptr) {
            return split_hole(hole, desired_word_size, actual_word_size, young_gen, used_change);
        }

        hole = find_best_fitting_hole(young_gen? _root_young : _root_old, min_word_size);

        if (hole != nullptr) {
            return split_hole(hole, min_word_size, actual_word_size, young_gen, used_change);
        }

    } else {
        HeapWord** root = young_gen ? _holes_young : _holes_old;
        HeapWord* hole = find_first_fitting_hole(root, desired_word_size, holes_exhausted);
        if (hole != nullptr) {
            return split_hole(hole, desired_word_size, actual_word_size, young_gen, used_change);
        }

        hole = find_first_fitting_hole(root, min_word_size, holes_exhausted);
        if (hole != nullptr) {
            return split_hole(hole, min_word_size, actual_word_size, young_gen, used_change);
        }
    }

    if (young_gen) {
        _hit_young--;
    } else {
        _hit_old--;
    }
    return nullptr;
}


HeapWord* G1RegionFreeSpaceTracker::find_young_hole(size_t min_word_size,
                                                    size_t desired_word_size,
                                                    size_t* actual_word_size,
                                                    size_t* used_change) {
    assert(!SafepointSynchronize::is_at_safepoint(), "do not reuse survivor holes at safepoint");
    bool dummy;
    return find_hole(min_word_size, desired_word_size, actual_word_size, true, used_change, &dummy);
}

HeapWord* G1RegionFreeSpaceTracker::find_old_hole(size_t min_word_size,
                                                  size_t desired_word_size,
                                                  size_t* actual_word_size,
                                                  size_t* used_change,
                                                  bool* holes_exhausted) {
    assert(SafepointSynchronize::is_at_safepoint(), "do not reuse old holes outside safepoint");
    assert(min_word_size <= desired_word_size, "must be");

    size_t aligned_min_word_size = align_up(min_word_size, G1CardTable::card_size_in_words());

    size_t min_word_size_gap = aligned_min_word_size - min_word_size;
    guarantee(CollectedHeap::min_fill_size() == 2, "must be");
    if (min_word_size_gap == 1 && min_word_size == desired_word_size) { // Can't fill one-word sized gap and we can't scale back desired_size and meeting desired_size >= min_word_size
        aligned_min_word_size += G1CardTable::card_size_in_words();
    }

    // Desired might be smaller now as we increased the min word size.
    size_t aligned_desired_word_size = align_up(desired_word_size, G1CardTable::card_size_in_words());

    // Min_word_size might be increased by a card, while desired not. Synchronize.
    aligned_desired_word_size = MAX2(aligned_min_word_size, aligned_desired_word_size);

    HeapWord* result = find_hole(aligned_min_word_size, aligned_desired_word_size, actual_word_size, false, used_change, holes_exhausted);
    if (result != nullptr) {
      // G1 does not support blocks that are larger than desired word size. Cut them.
      if (*actual_word_size > desired_word_size) {
        // Fill up.
        size_t net_word_size = desired_word_size;
        size_t filler_word_size = *actual_word_size - desired_word_size;
        if (filler_word_size == 1) {
            filler_word_size = CollectedHeap::min_fill_size();
            net_word_size--;
        }
        assert(net_word_size <= desired_word_size, "net too large: net %zu min %zu desired %zu actual %zu", net_word_size, min_word_size, desired_word_size, *actual_word_size);
        assert(net_word_size >= min_word_size,  "min too small: net %zu min %zu desired %zu actual %zu", net_word_size, min_word_size, desired_word_size, *actual_word_size);

        _g1h->fill_with_objects(result + net_word_size, filler_word_size);
        *actual_word_size = net_word_size;
      }
    }

    return result;
}

HeapWord* G1RegionFreeSpaceTracker::split_hole(HeapWord* hole, size_t word_size, size_t* actual_word_size, bool young_gen, size_t* used_change) {
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
    if (hole == region->top()) {
        region->set_top(region->top() + want_to_allocate);
        *used_change += want_to_allocate;
    } else {
        // indicate that we allocated below top().
        *used_change = 0;
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
    }

    
    //log_trace(gc_testing)("REMOVED : " PTR_FORMAT, p2i(hole));
    //return nullptr;
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
    {
        MutexLocker x(G1YoungDataStructure_lock, Mutex::_no_safepoint_check_flag);
        if (use_tree()) {            
            HeapWord* word = _holes_young[region->hrm_index()];
            while (word != nullptr) {
                //log_trace(gc_testing)("remove_region_young");
                remove_hole_tree(_root_young, word);
                word = get_next(word);
            }
        }
        _holes_young[region->hrm_index()] = nullptr;
    }
    {
        MutexLocker x(G1OldDataStructure_lock, Mutex::_no_safepoint_check_flag);
        if (use_tree()) {            
            HeapWord* word = _holes_old[region->hrm_index()];
            while (word != nullptr) {
                //log_trace(gc_testing)("remove_region_old");
                remove_hole_tree(_root_old, word);
                word = get_next(word);
            }
        }
        _holes_old[region->hrm_index()] = nullptr;
    }
}

void G1RegionFreeSpaceTracker::clean_cards_for_old_holes(uint index) {
  HeapWord* cur = _holes_old[index];
  if (cur == nullptr) { // If there is no hole list, nothing to do.
    return;
  }

  if (!is_eligible_region(index)) { // Nothing to do for regions not allocating into.
    return;
  }
  G1CardTable* table = _g1h->card_table();
  while (cur != nullptr) {
    size_t hole_size_in_words = get_size(cur);
    table->clear_MemRegion(MemRegion(cur, hole_size_in_words));
    cur = get_next(cur);
  }
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

    if (_all_young > 0) {
        log_trace(gc_testing)("Hit Ratio Young: %.4f Percent(%ld / %ld)", ((double)_hit_young) / _all_young * 100.0f, _hit_young, _all_young);
    }
    if (_all_old > 0) {
        log_trace(gc_testing)("Hit Ratio Old: %.4f Percent (%ld / %ld)", ((double)_hit_old) / _all_old * 100.0f, _hit_old, _all_old);
    }

}


void G1RegionFreeSpaceTracker::parse_list(HeapWord** list) {
    tty->print("digraph AllLists {\n");

    int count = 0;

    for (uint i = 0; i < _size; i++) {

        HeapWord* curr = list[i];

        while (curr != nullptr) {
            count++;
            tty->print(" N" PTR_FORMAT " [label=\" " PTR_FORMAT "\n(%ld)\nRegion :%d \"]\n", p2i(curr), p2i(curr), get_size(curr), i);
            if (get_next(curr) != nullptr) {
                tty->print("N" PTR_FORMAT " -> N" PTR_FORMAT "\n", p2i(curr), p2i(get_next(curr)));
            }
            curr = get_next(curr);
        }

    }

    tty->print("}\n");
    tty->print("Nodes in list : %d\n", count);
}


void G1RegionFreeSpaceTracker::parse_tree(HeapWord* root) {
    if (root == nullptr) {
        tty->print("Root is null\n");
        return;
    }
    if (root == _root_young) {
        return;
    }
    tty->print("digraph RBT {\n");
    int global_id = 0;
    visit_node(root, &global_id);
    tty->print("}\n");

}


bool G1RegionFreeSpaceTracker::check_synchronization(HeapWord* root, HeapWord** list) {

    bool check_lists_contains_tree = true;
    check_node(list, root, &check_lists_contains_tree);

    if (!check_lists_contains_tree) {
        return false;
    }    

    for (uint i = 0; i < _size; i++) {
        HeapWord* curr = list[i];
        while (curr != nullptr) {

            bool check_tree_contains_lists = false;
            contains_node(root, curr, &check_tree_contains_lists);

            if (!check_tree_contains_lists) {
                log_error(gc_testing)("Node " PTR_FORMAT " in lists but not in tree", p2i(curr));
                return false;
            }

            curr = get_next(curr);
        }
    } 
    return true;
}

void G1RegionFreeSpaceTracker::contains_node(HeapWord* tree_node, HeapWord* node, bool* check) {

    if (tree_node == nullptr || *check) return;

    *check = (tree_node == node);
    contains_node(get_left(tree_node), node, check);
    contains_node(get_right(tree_node), node, check);
}

void G1RegionFreeSpaceTracker::check_node(HeapWord** list, HeapWord* node, bool* check) {

    if (node == nullptr || !*check) return;

    *check = hole_in_lists(list, node);

    if (!*check) {
        log_error(gc_testing)("Node " PTR_FORMAT " in tree but not in lists", p2i(node));
    }

    check_node(list, get_left(node), check);
    check_node(list, get_right(node), check);
}

bool G1RegionFreeSpaceTracker::hole_in_lists(HeapWord** list, HeapWord* node) {

    for (uint i = 0; i < _size; i++) {
        HeapWord* curr = list[i];
        while (curr != nullptr) {
            if (curr == node) return true;
            curr = get_next(curr);
        }
    }
    return false;
}


void G1RegionFreeSpaceTracker::visit_node(HeapWord* node, int* global_id) {
    int id = *global_id;
    HeapWord* left = get_left(node);
    HeapWord* right = get_right(node);

    TreeHole* h = get_hole_tree(node);    
    G1HeapRegion* r = _g1h->heap_region_containing(node);

    tty->print_cr("word       = " PTR_FORMAT, p2i(node));
    tty->print_cr("tree       = " PTR_FORMAT, p2i(h));
    tty->print_cr("size       = %ld"  , h->size);
    tty->print_cr("next       = " PTR_FORMAT, p2i(h->next));
    tty->print_cr("parent     = " PTR_FORMAT, p2i(h->parent));
    tty->print_cr("left       = " PTR_FORMAT, p2i(h->left));
    tty->print_cr("right      = " PTR_FORMAT, p2i(h->right));
    tty->print_cr("red        = %d", h->red);
    tty->print_cr("pinned     = %d", r->has_pinned_objects());

    uint8_t* bytes = reinterpret_cast<uint8_t*>(get_hole_tree(node));

    tty->print_cr("Raw TreeHole bytes at " PTR_FORMAT ":", p2i(bytes));

    for (size_t i = 0; i < sizeof(TreeHole); i++) {
        if (i % 8 == 0) {
            tty->print("%3zu: ", i);
        }

        tty->print("%02x ", bytes[i]);

        if (i % 8 == 7 || i == sizeof(TreeHole) - 1) {
            tty->cr();
        }
    }


    tty->print(" N" PTR_FORMAT "[label=\"" PTR_FORMAT "\n(%ld)\",color=\"%s\"]\n", p2i(node), p2i(node), get_size(node), (is_red(node) ? "red" : "black"));
    (*global_id)++;
    if (left != nullptr) {
        tty->print(" N" PTR_FORMAT " -> N" PTR_FORMAT "\n", p2i(node), p2i(get_left(node)));
        visit_node(left, global_id);
    } else {
        tty->print(" N" PTR_FORMAT " -> N%d\n", p2i(node), *global_id);
        tty->print(" N%d [labl=NIL,color=\"black\"]\n", *global_id);
    }
    (*global_id)++;
    if (right != nullptr) {
        tty->print(" N" PTR_FORMAT " -> N" PTR_FORMAT "\n", p2i(node), p2i(get_right(node)));
        visit_node(right, global_id);
    } else {
        tty->print(" N" PTR_FORMAT " -> N%d\n", p2i(node), *global_id);
        tty->print(" N%d [labl=NIL,color=\"black\"]\n", *global_id);
    }

}



bool G1RegionFreeSpaceTracker::verify_tree(HeapWord* &root) const {

    // Empty tree is valid.
    if (root == nullptr) {
        return true;
    }

    // Property 2:
    // The root must be black.
    if (is_red(root)) {
        log_error(gc_testing)("RB violation: root is red");
        return false;
    }

    int black_height = 0;

    if (!verify_node(
            root,
            nullptr,
            nullptr,
            nullptr,
            black_height)) {

        return false;
    }

    return true;
}


bool G1RegionFreeSpaceTracker::verify_node(
        HeapWord* node,
        HeapWord* expected_parent,
        HeapWord* min_node,
        HeapWord* max_node,
        int& black_height) const {

    /*
     * nullptr represents a NIL leaf.
     *
     * NIL leaves are black and contribute one to the
     * black height.
     */
    if (node == nullptr) {
        black_height = 1;
        return true;
    }

    size_t size = get_size(node);


    /* ============================================================
     * Check parent pointer
     * ============================================================ */

    if (get_parent(node) != expected_parent) {
        log_error(gc_testing)("RB violation: wrong parent pointer at node %ld", size);
        return false;
    }


    /* ============================================================
     * Check BST property
     *
     * Everything in the left subtree must be:
     *
     *      < node
     *
     * Everything in the right subtree must be:
     *
     *      > node
     * ============================================================ */

    if (min_node != nullptr &&
        size < get_size(min_node)) {
        log_error(gc_testing)("BST violation: node %ld is not greater than lower bound %ld", size, get_size(min_node));
        return false;
    }

    if (max_node != nullptr &&
        size > get_size(max_node)) {
        log_error(gc_testing)("BST violation: node %ld is not smaller than upper bound %ld", size, get_size(max_node));
        return false;
    }


    /* ============================================================
     * Property 4:
     *
     * A red node cannot have a red child.
     * ============================================================ */

    if (is_red(node)) {

        if (get_left(node) != nullptr &&
            is_red(get_left(node))) {
            log_error(gc_testing)("RB violation: red node %ld has red left child %ld", size, get_size(get_left(node)));
            return false;
        }

        if (get_right(node) != nullptr &&
            is_red(get_right(node))) {
            log_error(gc_testing)("RB violation: red node %ld has red right child %ld", size, get_size(get_right(node)));
            return false;
        }
    }


    /* ============================================================
     * Verify left subtree
     *
     * Left subtree:
     *
     *      min < nodes < current
     * ============================================================ */

    int left_black_height = 0;

    if (!verify_node(
            get_left(node),
            node,
            min_node,
            node,
            left_black_height)) {

        return false;
    }


    /* ============================================================
     * Verify right subtree
     *
     * Right subtree:
     *
     *      current < nodes < max
     * ============================================================ */

    int right_black_height = 0;

    if (!verify_node(
            get_right(node),
            node,
            node,
            max_node,
            right_black_height)) {

        return false;
    }


    /* ============================================================
     * Property 5:
     *
     * Every path from a node to a NIL leaf must contain the
     * same number of black nodes.
     * ============================================================ */

    if (left_black_height != right_black_height) {
        log_error(gc_testing)("RB violation: black-height mismatch at node %ld (left = %d, right = %d)", size, left_black_height, right_black_height);
        return false;
    }


    /* ============================================================
     * Calculate black height of this subtree.
     * ============================================================ */

    black_height = left_black_height;

    if (is_black(node)) {
        black_height++;
    }

    return true;
}