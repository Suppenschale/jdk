#ifndef SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP
#define SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP

#include "oops/oop.hpp"
#include "oops/oopsHierarchy.hpp"

class G1RegionFreeSpaceTracker {

    private:
        G1CollectedHeap* _g1h;
        HeapWord** _holes_young;
        HeapWord** _holes_old;

        uint _size;

        size_t _min_hole_size_young;
        size_t _min_hole_size_old;

    public:
        G1RegionFreeSpaceTracker(G1CollectedHeap* heap);
        ~G1RegionFreeSpaceTracker();

        void initialize();

        void add_potential_hole_only_end(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size);


        HeapWord* find_hole_young(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);
        HeapWord* find_hole_old(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);

        //void remove_hole_humongous(G1HeapRegion* region);
        void clean_up_holes();
        void remove_hole(G1HeapRegion* region);

        void dump_regions();
        void dump_holes();
        void dump_hole_stats();

        size_t getSize(HeapWord* word) {
            oop obj = cast_to_oop(word);
            return obj->size();
        }

        void setNext(HeapWord* word, HeapWord* next) {
            oop obj = cast_to_oop(word);
            int header_size = obj->header_size();
            *(HeapWord**)(word + header_size) = next;
        }

        HeapWord* getNext(HeapWord* word) {
            oop obj = cast_to_oop(word);
            int header_size = obj->header_size();
            return *(HeapWord**)(word + header_size);
        }

};


#endif // SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP

