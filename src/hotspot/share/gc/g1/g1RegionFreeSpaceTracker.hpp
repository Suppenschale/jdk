#ifndef SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP
#define SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP

#include "oops/oop.hpp"
#include "oops/oopsHierarchy.hpp"

// Prefix of ListHole and TreeHole are the same
struct ListHole {
    size_t size;
    HeapWord* next;
};

struct TreeHole {
    size_t size;
    HeapWord* next;
    HeapWord* parent;
    HeapWord* left;
    HeapWord* right;
    bool red;
};

class G1RegionFreeSpaceTracker {

    private:
        G1CollectedHeap* _g1h;
        HeapWord** _holes_young;
        HeapWord** _holes_old;

        HeapWord* _root_young;
        HeapWord* _root_old;

        uint _size;

        size_t _min_hole_size_young;
        size_t _min_hole_size_old;

        bool _use_tree;

    public:
        G1RegionFreeSpaceTracker(G1CollectedHeap* heap);
        ~G1RegionFreeSpaceTracker();

        void initialize();

        void add_potential_hole_only_end(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size);
        void add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size);

        void set_hole_list(HeapWord* word, size_t size, HeapWord* next);
        ListHole* get_hole_list(HeapWord* word) const;

        void set_hole_tree(HeapWord* word);
        TreeHole* get_hole_tree(HeapWord* word) const;

        void set_size(HeapWord* word, size_t size);
        size_t get_size(HeapWord* word) const;

        void set_next(HeapWord* word, HeapWord* next);
        HeapWord* get_next(HeapWord* word) const;

        void set_parent(HeapWord* word, HeapWord* parent);
        HeapWord* get_parent(HeapWord* word) const;

        void set_left(HeapWord* word, HeapWord* left);
        HeapWord* get_left(HeapWord* word) const;

        void set_right(HeapWord* word, HeapWord* right);
        HeapWord* get_right(HeapWord* word) const;

        void set_color(HeapWord* word, bool red);
        bool get_color(HeapWord* word) const;

        void set_red(HeapWord* word);
        bool is_red(HeapWord* word) const;

        void set_black(HeapWord* word);
        bool is_black(HeapWord* word) const;

        void add_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* word, size_t size_in_words);
        void add_hole_tree(HeapWord* &root, HeapWord* word, size_t size_in_words);

        void transplant(HeapWord* &root, HeapWord* u, HeapWord* v);

        bool is_left(HeapWord* word) const;        
        bool is_right(HeapWord* word) const;

        void rb_insert_fixup(HeapWord* &root, HeapWord* word);
        void rb_remove_fixup(HeapWord* &root, HeapWord* x, HeapWord* x_parent);

        void left_rotate(HeapWord* &root, HeapWord* x);
        void right_rotate(HeapWord* &root, HeapWord* x);

        HeapWord* remove_hole_list(HeapWord** list, G1HeapRegion* region, HeapWord* remove);
        HeapWord* remove_hole_tree(HeapWord* &root, HeapWord* remove);

        HeapWord* find_exact_hole(HeapWord* &root, size_t size) const;
        HeapWord* find_best_fitting_hole(HeapWord* &root, size_t min_size) const;
        bool is_splittable(size_t min_size, size_t hole_size) const;

        HeapWord* find_hole_young(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);
        HeapWord* find_hole_old(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);

        HeapWord* split_hole(HeapWord* hole, size_t word_size, size_t* actual_word_size, bool young_gen);

        //void remove_hole_humongous(G1HeapRegion* region);
        void clean_up_holes_young();
        void clean_up_holes_old();
        void remove_region(G1HeapRegion* region);

        void dump_regions();
        void dump_holes();
        void dump_hole_stats();

        void dump_tree_node(HeapWord* word);

        void inorder_traversal(HeapWord* root);

};


#endif // SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP

