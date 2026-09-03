#ifndef SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP
#define SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP


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

// Openjdk has its own RB-Tree implementation, maybe it is useful to cherry-pick: https://bugs.openjdk.org/browse/JDK-8345314 or https://bugs.openjdk.org/browse/JDK-8349211
// or rebase with a later version.
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

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _hole_statistics_young;

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _hole_statistics_old;

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _hole_statistics_humongous;

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _object_min_statistics_young;

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _object_desired_statistics_young;


        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _object_min_statistics_old;

        HashTable<size_t, size_t,
                    65536,
                    AnyObj::C_HEAP,
                    mtGC> _object_desired_statistics_old;


        size_t hit_young;
        size_t all_young;

        size_t hit_old;
        size_t all_old;


    public:
        G1RegionFreeSpaceTracker(G1CollectedHeap* heap);

        void initialize();

        bool add_potential_survivor_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words);
        bool add_potential_humongous_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words);
        bool add_potential_old_hole(G1HeapRegion* region, HeapWord* word, size_t size_in_words);

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
        HeapWord* find_first_fitting_hole(HeapWord** list, size_t min_size) const;
        HeapWord* find_best_fitting_hole(HeapWord* &root, size_t min_size) const;
        bool is_splittable(size_t min_size, size_t hole_size) const;

        HeapWord* find_hole(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size, bool young_gen);
        HeapWord* find_young_hole(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);
        HeapWord* find_old_hole(size_t min_word_size, size_t desired_word_size, size_t* actual_word_size);

        HeapWord* split_hole(HeapWord* hole, size_t word_size, size_t* actual_word_size, bool young_gen);

        void clean_up_young_holes();
        void clean_up_old_holes();
        void remove_region(G1HeapRegion* region);

        void dump_tree_node(HeapWord* word);
        void inorder_traversal(HeapWord* root);

        void increment_young_min_statistic(size_t size_in_words);
        void increment_young_desired_statistic(size_t size_in_words);
        void increment_old_min_statistic(size_t size_in_words);
        void increment_old_desired_statistic(size_t size_in_words);
        void print_statistics() const;

};


#endif // SHARE_GC_G1_G1REGIONFREESPACETRACKER_HPP

