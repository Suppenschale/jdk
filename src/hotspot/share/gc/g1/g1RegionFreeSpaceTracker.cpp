
#include "gc/g1/g1AllocRegion.hpp"
#include "gc/g1/g1CollectedHeap.inline.hpp"
#include "gc/g1/g1RegionFreeSpaceTracker.hpp"
#include "logging/log.hpp"


G1RegionFreeSpaceTracker::G1RegionFreeSpaceTracker(G1CollectedHeap* heap) :
    _g1h(heap),
    _holes_young(nullptr),
    _holes_old(nullptr),
    _size(0),
    _min_hole_size_young(0),
    _min_hole_size_old(0)
{
    log_trace(gc_testing)("Init G1RegionFreeSpaceTracker");
}

G1RegionFreeSpaceTracker::~G1RegionFreeSpaceTracker() {
}

void G1RegionFreeSpaceTracker::initialize() {

    _size = _g1h->max_num_regions();
    _min_hole_size_young = _g1h->min_fill_size() + 2;
    _min_hole_size_old  = _g1h->min_fill_size() + 2+ 30;
    _holes_young = NEW_C_HEAP_ARRAY(HeapWord*, _size, mtGC);
    _holes_old = NEW_C_HEAP_ARRAY(HeapWord*, _size, mtGC);
    for (uint i = 0; i < _size; i++) {
        _holes_young[i] = nullptr;
        _holes_old[i] = nullptr;
    }
    log_trace(gc_testing)("Create hole array (%u regions)", _g1h->max_num_regions());
    dump_holes();

}

void G1RegionFreeSpaceTracker::add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size) {
    size_t size_in_words = size / HeapWordSize;

    if (size_in_words < _min_hole_size_young) {
        return;
    }

    _holes_young[region->hrm_index()] = word;
    set_size(word, size_in_words);
    size_t size_ = get_size(word);
    log_trace(gc_testing)("\tAdd hole (YOUNG) at: " PTR_FORMAT ", size = %7lu", p2i(word), size_);
}

void G1RegionFreeSpaceTracker::add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {

    if (size_in_words < _min_hole_size_old) {
        log_trace(gc_testing)("%ld < %ld", size_in_words, _min_hole_size_old);
        return;
    }

    _holes_old[region->hrm_index()] = word;
    set_size(word, size_in_words);
    set_next(word, region->end());
    size_t size_ = get_size(word);
    HeapWord* next_ = get_next(word);
    log_trace(gc_testing)("\tAdd hole (OLD) at: " PTR_FORMAT ", size = %7lu, next = " PTR_FORMAT, p2i(word), size_, p2i(next_));
}

void G1RegionFreeSpaceTracker::add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words) {
    
    if (size_in_words < _min_hole_size_old) {
        return;
    }

    HeapWord* head = _holes_old[region->hrm_index()];

    if (head == nullptr) {
        _holes_old[region->hrm_index()] = word;
        set_size(word, size_in_words);
        set_next(word, region->end());
    } else if (word < head) {
        _holes_old[region->hrm_index()] = word;
        set_size(word, size_in_words);
        set_next(word, head);
    } else {
        //Insert after head
        HeapWord* cur = head;
        HeapWord* prev = head;
        while (cur < word) {
            prev = cur;
            cur = get_next(cur);
        }
        if (cur != word) {
            set_next(word, get_next(prev));
            set_next(prev, word);
            set_size(word, size_in_words);
        }
    }



    /*if (_holes_old[region->hrm_index()] != nullptr) {
        HeapWord* obj = _holes_old[region->hrm_index()];
        set_next(word, obj);
    } else {        
        set_next(word, region->end());
    }
    set_size(word, size_in_words);
    _holes_old[region->hrm_index()] = word;*/
    size_t size_ = get_size(word);
    HeapWord* next_ = get_next(word);

    log_trace(gc_testing)("\tAdd hole (OLD) at: " PTR_FORMAT ", size = %7lu, next = " PTR_FORMAT, p2i(word), size_, p2i(next_));

    HeapWord* cur = _holes_old[region->hrm_index()];
    next_ = nullptr;

    int i = 0;
    do {
        next_ = get_next(cur);
        log_trace(gc_testing)("\t\tRegion %d Hole %3d: " PTR_FORMAT ", size = %7lu, next = " PTR_FORMAT " (" PTR_FORMAT ")"
            ,region->hrm_index(), i, p2i(cur), get_size(cur), p2i(next_), p2i(region->end()));
        i++;
        cur = next_;
        if (i > 500) {
            break;
        }
    } while (cur != region->end());
}

HeapWord* G1RegionFreeSpaceTracker::find_hole_young(size_t min_word_size,
                                                    size_t desired_word_size,
                                                    size_t* actual_word_size) {

    log_trace(gc_testing)("Find hole (YOUNG) of size %lu", desired_word_size);
    for (uint i = 0; i < _size; i++) {
        HeapWord* obj = _holes_young[i];
        if (obj != nullptr) {
            size_t available = get_size(obj);
            size_t want_to_allocate = MIN2(available, desired_word_size);
            size_t remaining = available - want_to_allocate;   

            log_trace(gc_testing)("\tHole %d: %ld", i, available);

            if (want_to_allocate >= min_word_size && remaining > _min_hole_size_young) {
                log_trace(gc_testing)("Hole index : %d", i);             

                HeapWord* dummy = obj + want_to_allocate;
                
                log_trace(gc_testing)("obj   (" PTR_FORMAT ")", p2i(obj));
                log_trace(gc_testing)("dummy (" PTR_FORMAT ")", p2i(dummy));
                G1HeapRegion* obj_region = _g1h->heap_region_containing(obj);
                G1HeapRegion* region = _g1h->heap_region_containing(dummy);
                log_trace(gc_testing)("top: %lu, end: %lu, diff: %lu, size: %lu, rem: %lu",
                p2i(region->top()), p2i(region->end()), pointer_delta(region->end(), region->top()), want_to_allocate, remaining);

                log_trace(gc_testing)("obj   region: %u", obj_region->hrm_index());
                log_trace(gc_testing)("dummy region: %u", region->hrm_index());

                region->fill_with_dummy_object(obj, want_to_allocate);
                region->fill_with_dummy_object(dummy, remaining);
                set_size(dummy, remaining);
                set_next(dummy, region->end());

                log_trace(gc_testing)("Cast obj");
                oop o = cast_to_oop(obj);
                log_trace(gc_testing)("size: %ld", o->size());

                log_trace(gc_testing)("Cast dummy");
                o = cast_to_oop(dummy);
                log_trace(gc_testing)("size: %ld", o->size());

                *actual_word_size = want_to_allocate;
                _holes_young[i] = dummy;
                log_trace(gc_testing)("Hole returned of size %lu (remaining : %lu)", want_to_allocate, remaining);
                //log_trace(gc_testing)("Remaining hole : %lu", get_size(dummy));
                //return nullptr;

                HeapWord* word = obj;
                int i = 0;
                while (word != region->end()) {

                    size_t size = cast_to_oop(word)->size();
                    log_trace(gc_testing)("Object %d of size %lu (" PTR_FORMAT " / " PTR_FORMAT " (%lu))", 
                        i, size, p2i(word), p2i(region->end()), pointer_delta(region->end(), word));
                    word += size;
                    i++;
                }


                return obj;
            }
        }
    }

    return nullptr;
}

HeapWord* G1RegionFreeSpaceTracker::find_hole_old(size_t min_word_size,
                                                  size_t desired_word_size,
                                                  size_t* actual_word_size) {

    log_trace(gc_testing)("Find hole (OLD) of size %lu", desired_word_size);
    //return nullptr;
    for (uint i = 0; i < _size; i++) {
        HeapWord* obj = _holes_old[i];
        if (obj != nullptr) {

            G1HeapRegion* region = _g1h->heap_region_containing(obj);
            HeapWord* next = nullptr;
            int j = 0;

            do {

                size_t available = get_size(obj);
                size_t want_to_allocate = MIN2(available, desired_word_size);
                size_t remaining = available - want_to_allocate;  
                next = get_next(obj);

                log_trace(gc_testing)("\tHole %d (%d): %lu", i, j, available);

                if (want_to_allocate >= min_word_size && remaining > _min_hole_size_old) {
                    log_trace(gc_testing)("Hole index : %d", i);              

                    HeapWord* dummy = obj + want_to_allocate;
                    
                    log_trace(gc_testing)("obj   (" PTR_FORMAT ")", p2i(obj));
                    log_trace(gc_testing)("dummy (" PTR_FORMAT ")", p2i(dummy));
                    G1HeapRegion* obj_region = _g1h->heap_region_containing(obj);
                    log_trace(gc_testing)("top: %lu, end: %lu, diff: %lu, size: %lu, rem: %lu",
                    p2i(region->top()), p2i(region->end()), pointer_delta(region->end(), region->top()), want_to_allocate, remaining);

                    log_trace(gc_testing)("obj   region: %u", obj_region->hrm_index());
                    log_trace(gc_testing)("dummy region: %u", region->hrm_index());

                    region->fill_with_dummy_object(obj, want_to_allocate);
                    region->fill_with_dummy_object(dummy, remaining);
                    set_size(dummy, remaining);
                    set_next(dummy, next);

                    log_trace(gc_testing)("Cast obj");
                    oop o = cast_to_oop(obj);
                    log_trace(gc_testing)("size: %ld", o->size());

                    log_trace(gc_testing)("Cast dummy");
                    o = cast_to_oop(dummy);
                    log_trace(gc_testing)("size: %ld", o->size());

                    *actual_word_size = want_to_allocate;
                    _holes_young[i] = dummy;
                    log_trace(gc_testing)("Hole returned of size %lu (remaining : %lu)", want_to_allocate, remaining);
                    //log_trace(gc_testing)("Remaining hole : %lu", get_size(dummy));
                    //return nullptr;

                   /* HeapWord* word = obj;
                    int i = 0;
                    while (word != region->end()) {

                        size_t size = cast_to_oop(word)->size();
                        log_trace(gc_testing)("Object %d of size %lu (" PTR_FORMAT " / " PTR_FORMAT " (%lu))", 
                            i, size, p2i(word), p2i(region->end()), pointer_delta(region->end(), word));
                        word += size;
                        i++;
                    }*/


                    return obj;
                }
                j++;
                obj = next;
            } while (next != region->end() && false);
        }
    }

    return nullptr;
}

void G1RegionFreeSpaceTracker::clean_up_holes_young() {
    log_trace(gc_testing)("Clean up holes young");
    for (uint i = 0; i < _size; i++) {
        _holes_young[i] = nullptr;
    }
}

void G1RegionFreeSpaceTracker::clean_up_holes_old() {
    log_trace(gc_testing)("Clean up holes old");
    for (uint i = 0; i < _size; i++) {
        _holes_old[i] = nullptr;
    }
}

void G1RegionFreeSpaceTracker::remove_region(G1HeapRegion* region) {
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