#include <iostream>
#include <unordered_map>
#include <list>
#include <random>
#include <cassert>

using namespace std;

int page_hits = 0;

class LIRS_bad_algo: public std::exception{
    private:
    char *msg;

    public:
    LIRS_bad_algo(char *msg){
        this->msg = msg;
    }
    char* what(){
        return msg;
    }
};

typedef enum {
    LIR = 1,
    HIR = 2,
    INVALID = 4
}block_type;

typedef enum {
    NOT_IN_QUEUE = 0,
    HIR_QUEUE  = 1,
    LIRS_QUEUE = 2
} lirs_location_t;

struct page_entry_t{
    block_type blk_type = INVALID;
    int64_t page_num = -1;
    page_entry_t* next = NULL;
    page_entry_t* prev = NULL;

    uint8_t location = NOT_IN_QUEUE;
    bool in_memory = false;

    page_entry_t(int64_t page_no, page_entry_t* next, page_entry_t *prev){
        this->page_num = page_no;
        this->next = next;
        this->prev = prev;
        blk_type = INVALID;
    }
};

class lirs_queue_t{
    private:
    page_entry_t *head;
    page_entry_t *tail;

    public:
    lirs_queue_t(){
        head = new page_entry_t(-1, NULL, NULL);
        tail = new page_entry_t(-2, head, NULL);
        head->next = tail;
    }

    void push_front(page_entry_t *page){
        page_entry_t *prev = tail->prev;
        prev->next = page;
        page->prev = prev;
        page->next = tail;
        tail->prev = page;
    }

    void push_back(page_entry_t *page){
        page_entry_t *next = head->next;
        head->next = page;
        page->prev = head;
        page->next = next;
        next->prev = page;
    }

    void remove(page_entry_t *page){
        if(page == head || page == tail){
            string *msg = new string("Cannot remove head or tail pointer!");
            throw(msg[0]);
        }
        page_entry_t *prev = page->prev;
        prev->next = page->next;
        page->next->prev = prev;
        page->next = page->prev = NULL;
    }

    page_entry_t *back(){
        return head->next;
    }

    page_entry_t *front(){
        return tail->prev;
    }
    page_entry_t *pop_back(){
        auto ret = back();
        remove(head->next);
        return ret;
    }
    page_entry_t *pop_front(){
        auto ret = front();
        remove(ret);
        return ret;
    }
};

class LIRS{
    public:
    uint32_t lir_blk_sz = 0;
    uint32_t hir_blk_sz = 0;
    uint32_t lir_left = 0;
    uint32_t hir_left = 0;
    list<page_entry_t*> *lirs_queue;
    list<page_entry_t*> *hir_queue;
    unordered_map<int, page_entry_t*> page_entries;
    void _prune_stack(){
        while(!lirs_queue->empty() && lirs_queue->back()->blk_type != LIR){
            page_entry_t *back = lirs_queue->back();
            lirs_queue->pop_back();
            if(!(back->location&HIR_QUEUE)){
                page_entries.erase(back->page_num);
                delete back;
            }
            else{
                back->location &= ~LIRS_QUEUE;
            }
        }
        assert(!(lirs_queue->empty()) && "lirs_queue is empty! IN: _prune_stack");
    }

    page_entry_t* access_lir(page_entry_t* page){ // hand traced
        if(lirs_queue->back() == page){
            lirs_queue->pop_back();
            lirs_queue->push_front(page);
            _prune_stack();
        }
        else{
            lirs_queue->remove(page);
            lirs_queue->push_front(page);
        }
        return page;
    }
    page_entry_t* access_resident_hir(page_entry_t* page){
        if(page->location&LIRS_QUEUE){ // hand traced
            page->blk_type = LIR;
            hir_queue->remove(page);
            page->location &= ~HIR_QUEUE;

            lirs_queue->remove(page);
            lirs_queue->push_front(page);

            auto base_lir = lirs_queue->back();
            lirs_queue->pop_back();
            base_lir->blk_type = HIR;
            base_lir->location |= HIR_QUEUE;
            base_lir->location &= ~LIRS_QUEUE;
            hir_queue->push_back(base_lir);
            _prune_stack();
        }
        else{ // TODO: trace by hand
            hir_queue->remove(page);

            lirs_queue->push_front(page);
            hir_queue->push_back(page);

            page->location |= LIRS_QUEUE;
        }
        return page;
    }
    page_entry_t* access_non_resident_hir(page_entry_t* page){ // hand traced
        assert(!hir_queue->empty() && "hir_queue is empty! IN: access_non_resident_hir");
        auto outgoing = hir_queue->front();
        hir_queue->pop_front();
        if(!(outgoing->location&LIRS_QUEUE)){
            page_entries.erase(outgoing->page_num);
            delete outgoing;
        }
        else{
            outgoing->location &= ~HIR_QUEUE;
            outgoing->in_memory = false;
        }
        if(page->location&LIRS_QUEUE){
            lirs_queue->remove(page);
            lirs_queue->push_front(page);

            page->blk_type = LIR;
            page->in_memory = true;
            page->location &= ~HIR_QUEUE;

            auto back = lirs_queue->back();
            lirs_queue->pop_back();

            back->blk_type = HIR;
            back->location &= ~LIRS_QUEUE;
            back->location |= HIR_QUEUE;
            hir_queue->push_back(back);
            _prune_stack();
        }
        else{
            lirs_queue->push_front(page);
            hir_queue->push_back(page);
            page->in_memory = true;
            page->location |= HIR_QUEUE;
            page->location |= LIRS_QUEUE;
            page->blk_type = HIR;
        }
        return page;
    }
    page_entry_t *_get_page(uint32_t page_no){
        page_entry_t *page = NULL;
        if(this->page_entries.find(page_no) != this->page_entries.end()){
            page = this->page_entries[page_no];
        }
        if(page == NULL && (this->hir_left || this->lir_left)){
            if(this->lir_left){
                page = new page_entry_t(page_no, NULL, NULL);
                page_entries[page_no] = page;
                page->location |= LIRS_QUEUE;
                page->in_memory = true;
                page->blk_type = LIR;
                lirs_queue->push_front(page);
                (this->lir_left)--;
            }
            else{
                page = new page_entry_t(page_no, NULL, NULL);
                page_entries[page_no] = page;
                page->location |= HIR_QUEUE;
                page->location |= LIRS_QUEUE;
                page->in_memory = true;
                page->blk_type = HIR;
                lirs_queue->push_front(page);
                hir_queue->push_back(page);
                (this->hir_left)--;
            }
        }
        else if(page == NULL){
            // HIR non-resident block
            page = new page_entry_t(page_no, NULL, NULL);
            page_entries[page_no] = page;
            access_non_resident_hir(page);
        }
        else if(page->blk_type == LIR){
            // LIR Block
            page_hits++;
            return access_lir(page);
        }
        else if(page->in_memory){
            // resident HIR
            page_hits++;
            access_resident_hir(page);
        }
        else if(page->blk_type == HIR){
            access_non_resident_hir(page);
        }
        return page;
    }


    public:
    LIRS(uint32_t lir_blk_sz, uint32_t hir_blk_sz){
        this->lir_blk_sz = lir_blk_sz;
        this->hir_blk_sz = hir_blk_sz;
        this->lir_left = lir_blk_sz;
        this->hir_left = hir_blk_sz;
        lirs_queue = new list<page_entry_t*>();
        hir_queue = new list<page_entry_t*>();
    }

    ~LIRS(){
        /*
        TODO: free the doubly-linked-list from head to tail
        */
        while(!lirs_queue->empty()){
            lirs_queue->pop_front();
        }
        delete lirs_queue;
         while(!hir_queue->empty()){
            hir_queue->pop_front();
        }
        delete hir_queue;
        for(auto node: page_entries)
            delete node.second;
    }
    page_entry_t *get_page(uint32_t page_no){
        return _get_page(page_no);
    }
};

int main(void){
    int distribution[]   = {1, 1, 1, 2, 2, 3, 3, 4, 5, 6, 7, 8, 9, 10};
    int access_pattern[] = {8, 3, 7, 1, 2, 3, 5, 10, 1, 8, 3, 1, 5, 1, 4, 1, 7, 1, 8, 4, 8};
    int query_count = 100000;
    random_device dev;
    mt19937 rng(dev());
    //uniform_int_distribution<std::mt19937::result_type> dist6(0, (sizeof(distribution)/sizeof(int))-1);
    uniform_int_distribution<std::mt19937::result_type> dist6(0, 100000);
    LIRS lirs(950,50);
    cout << "No. of Queries: " << query_count << endl;
    for(int i = 0; i < query_count; i++){
        //int index = dist6(rng);
        //cout<< "["<< i+1 << "]Index: " << index << endl;
        int query = dist6(rng);
        int prev = page_hits;
        lirs.get_page(query);
        //cout << "querying: " << query << ((prev != page_hits)? " HIT!" : " MISS!") << endl;
    }
    cout << "page_hits: " << page_hits << " : " << (float)(((float)page_hits/(float)query_count)*100) << "%" << endl;
}

// int main(void){
//     int access_pattern[] = {8, 3, 7, 1, 2, 3, 5, 10, 1};
//     LIRS lirs(3,2);
//     int *nptr = NULL;
//     for(auto q: access_pattern){
//         cout << "Accessing: " << q << endl;
//         lirs.get_page(q);
//     }
//     cout << "lirs_queue: ";
//     for(auto i: *lirs.lirs_queue)
//         cout << i->page_num << " ";
//     cout << endl;
//     cout << "hir_queue: ";
//     for(auto i: *lirs.hir_queue)
//         cout << i->page_num << " ";
//     cout << endl;
//     for(auto e: lirs.page_entries){
//         cout << e.first << " " << e.second->in_memory << " " << (e.second->location&HIR_QUEUE) << " " << (e.second->location&LIRS_QUEUE) << " " << (e.second->blk_type) << endl;
//     }
// }