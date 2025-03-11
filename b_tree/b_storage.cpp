#include <b_storage.h>

int write_block(const void *buff, size_t buff_size, int fd, off_t offset){
    int to_write = buff_size;
    int written  = 0;
    int pos      = 0;
    if(offset != -1)
        lseek(fd, offset, SEEK_SET);
    
    while(to_write != 0 && (written = write(fd, buff+pos, to_write)) != 0){
        if(written == -1){
            if(errno == EAGAIN) continue;
            perror("write");
            return -1;
        }
        to_write -= written;
        pos += written;
    }
    return buff_size - to_write;
}
int read_block(void *buff, size_t buff_size, int fd, off_t offset){
    int to_read     = buff_size;
    int have_read   = 0;
    int pos         = 0;
    if(offset != -1)
        lseek(fd, offset, SEEK_SET);
    
    while(to_read != 0 && (have_read = read(fd, buff+pos, to_read)) != 0){
        if(have_read == -1){
            if(errno == EAGAIN) continue;
            perror("read");
            return -1;
        }
        to_read -= have_read;
        pos += have_read;
    }

    return buff_size - to_read;
}

int sync_page(int db_file, page_t *page){
    if(write_block(page->page_buffer, page->page_size, db_file, page->page_loc) != page->page_size){
        printf("sync_page: Page Sync Failed: Location: %d\n", page->page_loc);
        return -1;
    }
    if(LOGGING_ENABLED) printf("sync_page: Page Sync success: Location: %d\n", page->page_loc);
    return 0;
}
int parse_page(void *page, size_t page_size, page_content_t *page_content){
    printf("Parsing page starting at location: %p\n", page);
    page_content->count         = (u_int32_t*) page;
    page_content->is_leaf       = (u_int32_t*) (page+sizeof(u_int32_t));
    page_content->ptrs          = malloc((MAX_DEGREE)*sizeof(page_ptr_t*));
    page_content->db_entries    = malloc((MAX_TUPLES_COUNT)*sizeof(db_entry_t*));

    //u_int32_t interval      = sizeof(page_ptr_t)+sizeof(db_entry_t);
    u_int32_t interval = (32/8+64+64/8);
    // void  *tuple_start  = (page + sizeof(page_content->count) + sizeof(page_content->is_leaf));
    void  *tuple_start  = (page + 32/8 + 32/8);
    for(int start = 0; start < MAX_TUPLES_COUNT; start++){
        page_content->ptrs[start] = (page_ptr_t*)(tuple_start+start*interval);
        page_content->db_entries[start] = (db_entry_t*)(tuple_start+start*interval+(32/8));
    }
    page_content->ptrs[MAX_DEGREE-1] = (page_ptr_t*)(tuple_start+(MAX_DEGREE-1)*interval);
    return 0;
}

int add_page(int db_file, size_t page_size, char *buff){
    int64_t ret   = lseek(db_file, 0, SEEK_END);
    if(LOGGING_ENABLED) printf("add_page: adding a page at %ld\n", ret);
    memset(buff, '\0', page_size);
    if(write_block(buff, page_size, db_file, ret) != page_size){
        printf("ERROR: add_page: Unable to flush data to disk: Offset: %ld\n", ret);
        return -1;
    }
    if(LOGGING_ENABLED)printf("add_page: Added a page at %ld\n", ret);
    return ret;
}

page_t *load_page(int db_file, page_ptr_t page_location){
    if(LOGGING_ENABLED)printf("load_page: Loading page at location: %d\n", page_location);
    page_t *page            = malloc(sizeof(page_t));
    page->page_loc          = page_location;
    page->page_size         = PAGE_SIZE;
    page->page_buffer    = (char*) malloc(PAGE_SIZE*sizeof(char));
    page->page_content      = malloc(sizeof(page_content_t));
    if(read_block(page->page_buffer, page->page_size, db_file, page->page_loc) != PAGE_SIZE ||
       parse_page(page->page_buffer, page->page_size, page->page_content) != 0){
        free(page->page_buffer);
        free(page->page_content);
        free(page);
        printf("load_page: Unable to load or parse page\n");
        return NULL;
    }
    if(LOGGING_ENABLED)printf("load_page: Successfully loaded page at location: %d[%d] (has %d keys)\n", page->page_loc, *(page->page_content->is_leaf), *(page->page_content->count));
    if(LOGGING_ENABLED)printf("Keys:\n\t");
    if(LOGGING_ENABLED && *(page->page_content->count) > 1){
        for(int i = 0; i < (int)(*(page->page_content->count)); i++){
            printf("%s ", (page->page_content->db_entries[i]->user_id));
        }
        printf("\n");
    }
    return page;
}

page_t *get_new_page(int db_file, u_int32_t page_size){
    page_t *new_page        = malloc(sizeof(page_t));
    if(!new_page){
        perror("malloc");
        return NULL;
    }
    new_page->page_size     = page_size;
    new_page->page_buffer = malloc(page_size*sizeof(char));
    if(!new_page->page_buffer){
        perror("malloc");
        return NULL;
    }
    new_page->page_content  = malloc(sizeof(page_content_t));
    if(!(new_page->page_content)){
        perror("malloc");
        return NULL;
    }
    if((new_page->page_loc = add_page(db_file, page_size, new_page->page_buffer)) == -1 ||
       parse_page(new_page->page_buffer, page_size, new_page->page_content) == -1){
        free(new_page->page_buffer);
        free(new_page->page_content);
        free(new_page);
        printf("ERROR: get_new_page: Failed allocate a new page.\n");
        return NULL;
    }
    *(new_page->page_content->is_leaf) = 1;
    *(new_page->page_content->count)   = 0;
    sync_page(db_file, new_page);
    if(LOGGING_ENABLED) printf("Successfully allocated a new page: Loc: %d\n", new_page->page_loc);
    return new_page;
}

page_t *btree_split(page_t *page, u_int32_t index, int db_file){
    // page at 'index' is guarenteed to be full i.e., count == MAX_TUPLES_COUNT
    int64_t tuples_to_copy = MIN_TUPLES_COUNT;
    page_t *right_page = get_new_page(db_file, page->page_size);
    page_t *left_page  = load_page(db_file, *(page->page_content->ptrs[index]));
    if(LOGGING_ENABLED) printf("btree_split: Got a request for page at index: %d split: tuple present: %d\n", index, *(left_page->page_content->count));
    *(right_page->page_content->count) = tuples_to_copy;
    *(left_page->page_content->count)  = MIN_TUPLES_COUNT; // one node will be shifted up to parent.
    *(right_page->page_content->is_leaf) = *(left_page->page_content->is_leaf);
    int start = MIN_DEGREE;
    for(int i = 0; i < MIN_DEGREE; i++){
        *(right_page->page_content->ptrs[i])       = *(left_page->page_content->ptrs[i+start]);
    }
    start = MIN_TUPLES_COUNT + 1;
    for(int i = 0; i < MIN_TUPLES_COUNT; i++){
        *(right_page->page_content->db_entries[i]) = *(left_page->page_content->db_entries[i+start]);
    }
    // shift forward the contents of the parent node to make way for the pointer of the right_page.
    for(int i = MAX_DEGREE-2 ; i >= (int64_t)(index+1); i--){
        *(page->page_content->ptrs[i+1]) = *(page->page_content->ptrs[i]);
    }
    for(int i = MAX_TUPLES_COUNT-2 ; i >= (int64_t)(index); i--){
        *(page->page_content->db_entries[i+1]) = *(page->page_content->db_entries[i]);
    }
    *(page->page_content->db_entries[index]) = *(left_page->page_content->db_entries[MIN_TUPLES_COUNT]);
    *(page->page_content->ptrs[index+1]) = right_page->page_loc;
    *(page->page_content->count) += 1;
    *(page->page_content->is_leaf) = 0;
    sync_page(db_file, page);
    free_page(db_file, left_page, 1);
    free_page(db_file, right_page, 1);
    return page;
}

int free_page(int db_file, page_t *page, u_int32_t do_write){
    if(page == NULL){
        printf("Page is already freed. Ignoring.\n");
        return 0;
    }
    if(do_write){
        if(write_block(page->page_buffer, page->page_size, db_file, page->page_loc) != page->page_size){
            printf("Failed to write page at offset: %d\n", page->page_loc);
            return -1;
        }
        else{
            if(LOGGING_ENABLED)printf("Page sync success. Page offset %d\n", page->page_loc);
        }
    }
    free(page->page_buffer);
    free(page->page_content->db_entries);
    free(page->page_content->ptrs);
    free(page->page_content);
    free(page);
    page = NULL;
    return 0;
}
int btree_insert(int db_file, size_t page_size, const db_entry_t *db_entry){
    if(LOGGING_ENABLED)printf("btree_insert: Got insert request: Username: %s\n", db_entry->user_id);
    page_t *header = NULL;
    page_t *parent = NULL;
    page_t *child  = NULL;

    if((header = load_page(db_file, 0)) == NULL){
        printf("Unable to load the header\n");
        return -1;
    }
    if(*(header->page_content->count) == 0){
        if(LOGGING_ENABLED)printf("No entries in tree. Creating first node.\n");
        page_t *new_page = get_new_page(db_file, page_size);
        *(header->page_content->ptrs[0]) = new_page->page_loc;
        *(header->page_content->count) += 1;
        free_page(db_file, header, 1);
        free_page(db_file, new_page, 1);
        header = load_page(db_file, 0);
    }
    parent = load_page(db_file, *(header->page_content->ptrs[0]));
    
    if(*(parent->page_content->count) == MAX_TUPLES_COUNT){     // top page is full
        page_t *tmp = get_new_page(db_file,PAGE_SIZE);
        *(tmp->page_content->count)     = 0;
        *(tmp->page_content->is_leaf)   = 0;
        *(tmp->page_content->ptrs[0])   = *(header->page_content->ptrs[0]);
        *(header->page_content->ptrs[0])= tmp->page_loc;

        free_page(db_file, parent, 1);
        sync_page(db_file, tmp);
        parent = tmp;
        btree_split(parent, 0, db_file);
    }
    while(1){
        int index = 0;
        while(index < *(parent->page_content->count) && strncmp(db_entry->user_id, parent->page_content->db_entries[index]->user_id, sizeof(db_entry->user_id)) > 0){
            if(LOGGING_ENABLED) printf("Stepping forward as %s is smaller than %s\n", parent->page_content->db_entries[index]->user_id, db_entry->user_id);
            index++;
        }
        if(!(*(parent->page_content->is_leaf))){
            child = load_page(db_file, *(parent->page_content->ptrs[index]));
            if(child == NULL)
                return -1;
            int64_t tuples_in_child = *(child->page_content->count);
            free_page(db_file, child, 1);
            child = NULL;
            if(tuples_in_child == MAX_TUPLES_COUNT){
                btree_split(parent, index, db_file);
                while(index < *(parent->page_content->count) && strncmp(db_entry->user_id, parent->page_content->db_entries[index]->user_id, sizeof(db_entry->user_id)) > 0)
                    index++;
            }
            page_ptr_t loc = *(parent->page_content->ptrs[index]);
            free_page(db_file, parent, 1);
            parent = load_page(db_file, loc);
            continue;
        }
        // shift the nodes and insert the element -- Leaf is guarenteed to have space.
        if(LOGGING_ENABLED) printf("Leaf Node: %d inserting at index: %d\n", parent->page_loc, index);
        for(int i = *(parent->page_content->count)-1; i >= index; i--){
            *(parent->page_content->db_entries[i+1]) = *(parent->page_content->db_entries[i]);
        }
        *(parent->page_content->db_entries[index]) = *(db_entry);
        *(parent->page_content->count) += 1;
        free_page(db_file, parent, 1);
        break;
    }
    free_page(db_file, header, 1);
    return 0;
}

int btree_merge(int db_file, page_t *page, int index){
    page_t *left_page  = load_page(db_file, *(page->page_content->ptrs[index]));
    page_t *right_page = load_page(db_file, *(page->page_content->ptrs[index+1]));
    if(LOGGING_ENABLED) printf("btree_merge: merging:%s <-> %s <-> %s\n", (left_page->page_content->db_entries[*(left_page->page_content->count)-1]->user_id),
                                                                          (page->page_content->db_entries[index]->user_id),
                                                                          (right_page->page_content->db_entries[0]->user_id));
    //move down the key at the index
    *(left_page->page_content->db_entries[*(left_page->page_content->count)]) = *(page->page_content->db_entries[index]);
    *(left_page->page_content->count) += 1;
    //left shift the remainging nodes in parent after the index
    for(int i = index+1; i < (int)(*(page->page_content->count)); i++)
        *(page->page_content->db_entries[i-1]) = *(page->page_content->db_entries[i]);
    for(int i = index+2; i <= (int)(*(page->page_content->count)); i++)
        *(page->page_content->ptrs[i-1]) = *(page->page_content->ptrs[i]);
    *(page->page_content->count) -= 1;
    if(LOGGING_ENABLED) printf("btree_merge: parent node modification complete\n");
    // copy over all the elements from right child to left child
    for(int i = 0; i < (int)(*(right_page->page_content->count)); i++)
        *(left_page->page_content->db_entries[*(left_page->page_content->count)+i]) = *(right_page->page_content->db_entries[i]);
    if(LOGGING_ENABLED) printf("btree_merge: moved all keys from right to left\n");
    for(int i = 0; i <= (int)(*(right_page->page_content->count)); i++)
        *(left_page->page_content->ptrs[*(left_page->page_content->count)+i]) = *(right_page->page_content->ptrs[i]);
    if(LOGGING_ENABLED) printf("btree_merge: moved all ptrs from right to left\n");
    // delete right child
    *(left_page->page_content->count) += *(right_page->page_content->count);
    free_page(db_file, left_page, 1);
    free_page(db_file, right_page, 1);
    return 0;
}

int get_successor(int db_file, page_ptr_t page_offset, db_entry_t *db_entry){
    page_t *page = load_page(db_file, page_offset);
    int64_t count = *(page->page_content->count);
    if(count <= MIN_TUPLES_COUNT)
        return -1;
    *db_entry = *(page->page_content->db_entries[0]);
    btree_delete(db_file, page, db_entry->user_id, sizeof(db_entry->user_id));
    return 0;
}
int get_predecessor(int db_file, page_ptr_t page_offset, db_entry_t *db_entry){
    page_t *page = load_page(db_file, page_offset);
    int64_t count = *(page->page_content->count);
    if(count <= MIN_TUPLES_COUNT){
        free_page(db_file, page, 0);
        return -1;
    }
    *db_entry = *(page->page_content->db_entries[count-1]);
    btree_delete(db_file, page, db_entry->user_id, sizeof(db_entry->user_id));
    return 0;
}

int borrow_from_right(int db_file, page_t *parent, int index){
    page_t *left = load_page(db_file, *(parent->page_content->ptrs[index]));
    page_t *right = load_page(db_file, *(parent->page_content->ptrs[index+1]));

    if(*(right->page_content->count) <= MIN_TUPLES_COUNT){
        free_page(db_file, left, 0);
        free_page(db_file, right, 0);
        return -1;
    }
    // Move the key at `index` from parent to left
    *(left->page_content->db_entries[*(left->page_content->count)]) = *(parent->page_content->db_entries[index]);
    *(left->page_content->count) += 1;
    // Move the left most key from the right to the parent
    *(parent->page_content->db_entries[index]) = *(right->page_content->db_entries[0]);
    // reparent the left most child of right to the rightmost entry of the left
    *(left->page_content->ptrs[*(left->page_content->count)]) = *(right->page_content->ptrs[0]);
    // left shift all contents of right by one
    for(int i = 1; i <= (int)(*(right->page_content->count)); i++)
        *(right->page_content->ptrs[i-1]) = *(right->page_content->ptrs[i]);

    for(int i = 1; i < (int)(*(right->page_content->count)); i++)
        *(right->page_content->db_entries[i-1]) = *(right->page_content->db_entries[i]);
    // decrease only the count of right as incrasing the count of left is alread taken care of
    *(right->page_content->count) -= 1;
    free_page(db_file, left, 1);
    free_page(db_file, right, 1);
    sync_page(db_file, parent);
    return 0;
}

int borrow_from_left(int db_file, page_t *parent, int index){
    if(LOGGING_ENABLED) printf("borrow_from_left: parent Loc: %d, Index: %d\n", parent->page_loc, index);
    page_t *left = load_page(db_file, *(parent->page_content->ptrs[index-1]));
    page_t *right = load_page(db_file, *(parent->page_content->ptrs[index]));
    if(LOGGING_ENABLED) printf("Left has %d keys.\n", *(left->page_content->count));
    if(*(left->page_content->count) <= MIN_TUPLES_COUNT){
        free_page(db_file, left, 0);
        free_page(db_file, right, 0);
        if(LOGGING_ENABLED) printf("Left doesn't have enough keys\n. Exiting ...");
        return -1;
    }
    // shift the tuples in right by one place
    for(int i = *(right->page_content->count); i >= 0; i--)
        *(right->page_content->ptrs[i+1]) = *(right->page_content->ptrs[i]);
    for(int i = *(right->page_content->count) - 1; i >= 0; i--)
        *(right->page_content->db_entries[i+1]) = *(right->page_content->db_entries[i]);
    printf("borrow_from_left: Made space in right(at index) page.\n");
    // Move the key at `index` from parent to right
    *(right->page_content->db_entries[0]) = *(parent->page_content->db_entries[index]);
    // Move the right most key from the left to the parent
    *(parent->page_content->db_entries[index]) = *(left->page_content->db_entries[*(left->page_content->count)-1]);
    // reparent the right most child of left to the leftmost entry of the right
    *(right->page_content->ptrs[0]) = *(left->page_content->ptrs[*(left->page_content->count)]);
    // decrease only the count of right as incrasing the count of left is alread taken care of
    *(left->page_content->count) -= 1;
    *(right->page_content->count) += 1;
    printf("borrow_from_left: Changes complete.\n");
    free_page(db_file, left, 1);
    free_page(db_file, right, 1);
    sync_page(db_file, parent);
    return 0;
}
int64_t btree_delete(int db_file, page_t *page, const char *key, size_t key_length){
    if(LOGGING_ENABLED) printf("btree_delete: page_loc: %d, key: %s, first_entry: %s\n", page->page_loc, key, (page->page_content->db_entries[0]->user_id));
    int index = 0;
    if(*(page->page_content->count) == 0){
        printf("Node empty nothing to delete.\nThis happens when the tree is empty!\n");
        return -2;
    }
    while(index < (int)(*page->page_content->count) && strncmp(key, (page->page_content->db_entries[index]->user_id), key_length) > 0){
        if(LOGGING_ENABLED)printf("Stepping forward as %s is less than %s\n", (page->page_content->db_entries[index]->user_id), key);
        index++;
    }
    if(*(page->page_content->is_leaf) && strncmp(key, (page->page_content->db_entries[index]->user_id), key_length) != 0){
        if(LOGGING_ENABLED) printf("Key not found!\n");
        return -1;
    }
    if(index != (int)(*page->page_content->count) && strncmp(key, (page->page_content->db_entries[index]->user_id), key_length) == 0){
        if(LOGGING_ENABLED) printf("Got a match: Page: %d, Index: %d, Is Leaf: %d\n", page->page_loc, index, *(page->page_content->is_leaf));
        if(*(page->page_content->is_leaf)){
            // just delete it.
            printf("Deleting entry at index: %d\n", index);
            for(int i = index; i < (int)(*(page->page_content->count)-1); i++){
                *(page->page_content->db_entries[i]) = *(page->page_content->db_entries[i+1]);
            }
            *(page->page_content->count) -= 1;
            printf("After deleting, page has %d keys\n", *(page->page_content->count));
        }
        else{
            db_entry_t *db_entry = malloc(76);
            if(get_predecessor(db_file, *(page->page_content->ptrs[index]), db_entry) != -1 || get_successor(db_file, *(page->page_content->ptrs[index+1]), db_entry) != -1)
                *(page->page_content->db_entries[index]) = *db_entry;
            else{
                btree_merge(db_file, page, index);
                btree_delete(db_file, load_page(db_file, *(page->page_content->ptrs[index])), key, key_length);
                //*(page->page_content->count) -= 1; <-- Reducing the count of parent will be taken care of during btreee merge
            }
            free(db_entry);
        }
    }
    else{
        // try borrowing from left or right sibling
        // if borrowing doesn't work, merge and call delete on the index node.
        page_t *child = load_page(db_file, *(page->page_content->ptrs[index]));
        int child_entry_count = *(child->page_content->count);
        free_page(db_file, child, 0);
        if(LOGGING_ENABLED) printf("btree_delete: child has %d keys.\n", child_entry_count);
        if(child_entry_count > MIN_TUPLES_COUNT)
        {
            btree_delete(db_file, load_page(db_file, *(page->page_content->ptrs[index])), key, key_length);
        }
        else{
            int has_enough = 0;
            if(!has_enough && (index-1 >= 0)){
                if(LOGGING_ENABLED) printf("btree_delete: left sibling present. Will try to borrow from left\n");
                if((borrow_from_left(db_file, page, index) != -1)){
                    printf("btree_delete: borrowed from left.\n");
                    has_enough = 1;
                }
                else{
                    printf("btree_delete: Couldn't borrow from left.\n");
                }
            }
            if(!has_enough && index+1 <= (int)(*(page->page_content->count))){
                if(LOGGING_ENABLED) printf("btree_delete: right sibling present. Will try to borrow from right\n");
                if((borrow_from_right(db_file, page, index) != -1)){
                    printf("btree_delete: borrowed from right.\n");
                    has_enough = 1;
                }
                else{
                    printf("btree_delete: Couldn't borrow from right.\n");
                }
            }
            if(!has_enough && index+1 <= (int)(*(page->page_content->count))){
                if(LOGGING_ENABLED) printf("btree_delete: right sibling present. Will try to merge right with me\n");
                if((btree_merge(db_file, page, index) != -1)){
                    printf("btree_delete: merged right with me.\n");
                    has_enough = 1;
                }
                else{
                    printf("btree_delete: Couldn't merge right with me.\n");
                }
            }
            if(!has_enough && (index-1 >= 0)){
                index -= 1;
                if(LOGGING_ENABLED) printf("btree_delete: left sibling present. Will try to merge with left\n");
                if((btree_merge(db_file, page, index) != -1)){
                    printf("btree_delete: merged with left.\n");
                    has_enough = 1;
                }
                else{
                    printf("btree_delete: Couldn't merge with left.\n");
                }
            }
            btree_delete(db_file, load_page(db_file, *(page->page_content->ptrs[index])), key, key_length);
            // if((index-1 >= 0 && (borrow_from_left(db_file, page, index) != 1)) ||
            // (index+1 <= (int)(*(page->page_content->count)) && borrow_from_right(db_file, page, index) != -1) ||
            // (index+1 <= (int)(*(page->page_content->count)) && btree_merge(db_file, page, index) != -1))
            //     btree_delete(db_file, load_page(db_file, *(page->page_content->ptrs[index])), key, key_length);
            // else{
            //     index -= 1;
            //     btree_merge(db_file, page, index);
            //     btree_delete(db_file, load_page(db_file, *(page->page_content->ptrs[index])), key, key_length);
            // }
        }
    }
    if(LOGGING_ENABLED) printf("btree_delete: Page[%d]: Tuples present: %d\n", page->page_loc, *(page->page_content->count));
    if(*(page->page_content->count) == 0){
        if(LOGGING_ENABLED) printf("btree_delete: requesting root page delete\n");
        int ret = *(page->page_content->ptrs[index]);
        free_page(db_file, page, 1);
        return ret;
    }
    free_page(db_file, page, 1);
    return 0;
}

int btree_delete_start(int db_file, const char *key, size_t key_length){
    if(LOGGING_ENABLED)printf("Got a request to delete key: %s\n", key);
    page_t *header = NULL;
    int64_t parent_loc = -1;

    header = load_page(db_file, 0);
    parent_loc = btree_delete(db_file, load_page(db_file, *(header->page_content->ptrs[0])), key, key_length);
    if(parent_loc > 0){
        *(header->page_content->ptrs[0]) = parent_loc;
    }
    if(LOGGING_ENABLED) printf("btree_delete_start: freeing header\n");
    free_page(db_file, header, 1);
    return parent_loc;
}
int btree_find_worker(int db_file, page_ptr_t page_loc, const char *key, size_t key_length, tuple_info_t *tuple_info){
    int index = 0;

    page_t *page = load_page(db_file, page_loc);
    while(index < *(page->page_content->count) && strncmp(key, page->page_content->db_entries[index]->user_id, key_length) > 0)
        index++;
    if(index == *(page->page_content->count) && *(page->page_content->is_leaf))
        return -1;
    if(index <= *(page->page_content->count) && strncmp(key, page->page_content->db_entries[index]->user_id, key_length) == 0){
        tuple_info->index = index;
        tuple_info->page = page;
        return 0;
    }
    page_ptr_t search = *(page->page_content->ptrs[index]);
    free_page(db_file, page, 0);
    return btree_find_worker(db_file, search, key, key_length, tuple_info);
}
int btree_find(int db_file, const char *key, size_t key_length, tuple_info_t *tuple_info){
    page_t *header = load_page(db_file, 0);
    int retcode = -1;
    if(*(header->page_content->count) > 0)
        retcode = btree_find_worker(db_file, *(header->page_content->ptrs[0]), key, key_length, tuple_info);
    free_page(db_file, header, 0);
    return retcode;
}
int init_db_storage(int db_file, size_t page_size){
    struct stat stat_buf;
    if(fstat(db_file, &stat_buf) != 0){
        perror("fstat");
        return -1;
    }
    // printf("Database file size: %ld\n", stat_buf.st_size);
    if(stat_buf.st_size == 0){
        page_t *page = get_new_page(db_file, page_size);
        if(!page){
            // printf("ERROR: Failed to get a new page.\n");
            return -1;
        }
        // printf("Got a new page at file offset: %d\n", page->page_loc);
        *(page->page_content->is_leaf) = 0;
        free_page(db_file, page, 1);
    }
    return 0;
}