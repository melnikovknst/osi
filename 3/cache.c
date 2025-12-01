#include "cache.h"
#include "config.h"
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdatomic.h>

typedef struct block {
    size_t used;
    char  data[BLOCK_SZ];
} block_t;

struct record {
    char *key;
    uint64_t h;

    pthread_mutex_t m;
    pthread_cond_t  updated;
    block_t **blocks;
    size_t nblocks, capblocks;
    size_t total;
    int completed, canceled;
    int has_fetcher;

    int keep_on_complete; 
    size_t declared_length; 

    atomic_int refcnt; 
    int in_lru;

    record_t *prev, *next;

    struct entry *e;
};

struct entry {
    uint64_t h;
    char *key;
    record_t *rec;
    struct entry *next;
};

static struct bucket* bucket_of(cache_t *c, uint64_t h){ return &c->b[h & (c->nbuckets-1)]; }

static uint64_t fnv1a64(const char *s) {
    uint64_t h=1469598103934665603ULL;
    for (; *s; ++s) { 
        h^=(unsigned char)*s; 
        h*=1099511628211ULL; 
    }
    return h;
}

char *strdup_safe(const char *s) {
    if(!s) 
        return NULL; 
    size_t n = strlen(s) + 1; 
    char *p=malloc(n); 
    if(!p) 
        return NULL; 
    
    memcpy(p,s,n); 

    return p;
}

static record_t* rec_create(const char *key, uint64_t h) {
    record_t *r=calloc(1,sizeof *r); 
    if(!r) 
        return NULL;
    
    r->key=strdup_safe(key); 
    r->h=h;
    pthread_mutex_init(&r->m,NULL);
    pthread_cond_init(&r->updated,NULL);
    r->keep_on_complete=1;

    atomic_init(&r->refcnt, 1); 

    return r;
}

static void rec_free(record_t *r) {
    if(!r) return;
    for(size_t i=0;i<r->nblocks;i++) free(r->blocks[i]);
    free(r->blocks);
    free(r->key);
    pthread_mutex_destroy(&r->m);
    pthread_cond_destroy(&r->updated);
    free(r);
}

int cache_init(cache_t *c, size_t nbuckets, size_t soft) {
    if((nbuckets & (nbuckets-1)) != 0) 
        return -1;
    c->nbuckets=nbuckets;
    c->b = calloc(nbuckets, sizeof *c->b); if(!c->b) return -1;
    for(size_t i = 0; i < nbuckets; i++) 
        pthread_mutex_init(&c->b[i].m, NULL);

    pthread_mutex_init(&c->lru_m, NULL);
    c->lru_head = c->lru_tail = NULL;
    c->bytes_completed = 0;
    c->soft_limit = soft;
    c->hits = c->misses = c->stores = c->evicts = 0;

    return 0;
}
void cache_destroy(cache_t *c) {
    for(size_t i = 0; i < c->nbuckets; i++) {
        pthread_mutex_lock(&c->b[i].m);
        struct entry *e=c->b[i].head;
        while(e) {
            struct entry *n=e->next;
            if(e->rec) 
                if(atomic_fetch_sub(&e->rec->refcnt,1) == 1) 
                    rec_free(e->rec);

            free(e->key); free(e);
            e=n;
        }

        pthread_mutex_unlock(&c->b[i].m);
        pthread_mutex_destroy(&c->b[i].m);
    }

    free(c->b);
    pthread_mutex_destroy(&c->lru_m);
}

int cache_acquire(cache_t *c, const char *key, cache_acquire_t *out) {
    uint64_t h = fnv1a64(key);
    struct bucket *b=bucket_of(c,h);
    pthread_mutex_lock(&b->m);
    struct entry *e=b->head;
    while(e) {
        if(e->h == h && strcmp(e->key, key) == 0) 
            break; 
        e = e->next; 
    }

    record_t *r = NULL;
    if(!e) {
        e = calloc(1,sizeof *e);
        e->h = h; 
        e->key = strdup_safe(key);
        r = rec_create(key,h);
        e->rec = r;
        e->next = b->head;
        b->head = e;
        r->e = e;
        __atomic_add_fetch(&c->misses, 1, __ATOMIC_RELAXED);
        out->is_fetcher = 1;
    } else {
        r = e->rec;
        __atomic_add_fetch(&c->hits, 1, __ATOMIC_RELAXED);
        out->is_fetcher = 0;
    }
    atomic_fetch_add(&r->refcnt, 1);
    pthread_mutex_unlock(&b->m);

    pthread_mutex_lock(&r->m);
    if(!r->completed && !r->canceled && !r->has_fetcher) {
        r->has_fetcher=1; 
        out->is_fetcher=1;
    }

    out->rec = r;
    pthread_mutex_unlock(&r->m);

    return 0;
}

void cache_release(record_t *r) {
    if(!r) 
        return;
    if(atomic_fetch_sub(&r->refcnt, 1) == 1)
        rec_free(r);
}

static void lru_remove(cache_t *c, record_t *r) {
    if(!r->in_lru) 
        return;
    if(r->prev) 
        r->prev->next = r->next; 
    else 
        c->lru_head = r->next;
    if(r->next)
        r->next->prev = r->prev; 
    else
        c->lru_tail = r->prev;
    r->prev = r->next = NULL; 
    r->in_lru = 0;
}

static void lru_push_front(cache_t *c, record_t *r) {
    r->prev = NULL; 
    r->next = c->lru_head;
    if(c->lru_head) 
        c->lru_head->prev = r; 
    else 
        c->lru_tail = r;
    c->lru_head = r;
    r->in_lru = 1;
}

void rec_touch_lru(cache_t *c, record_t *r) {
    pthread_mutex_lock(&c->lru_m);
    if(r->in_lru) {
        lru_remove(c,r);
        lru_push_front(c,r);
    }
    pthread_mutex_unlock(&c->lru_m);
}

static void try_evict_until_soft(cache_t *c) {
    pthread_mutex_lock(&c->lru_m);
    while(c->bytes_completed > c->soft_limit && c->lru_tail) {
        record_t *r = c->lru_tail;
        lru_remove(c,r);
        c->bytes_completed -= r->total;
        __atomic_add_fetch(&c->evicts, 1, __ATOMIC_RELAXED);

        struct bucket *b=bucket_of(c, r->h);
        pthread_mutex_lock(&b->m);
        struct entry **pp=&b->head;
        while(*pp){
            if((*pp)->rec == r){ 
                struct entry *dead=*pp; *pp=dead->next;
                if(atomic_fetch_sub(&r->refcnt,1) == 1) {
                    pthread_mutex_unlock(&b->m); 
                    pthread_mutex_unlock(&c->lru_m); 
                    rec_free(r); 

                    return; 
                }
                free(dead->key); 
                free(dead); 

                break;
            }
            pp=&(*pp)->next;
        }
        pthread_mutex_unlock(&b->m);

        if(atomic_load(&r->refcnt) == 0) {
            pthread_mutex_unlock(&c->lru_m); 
            rec_free(r); 
            return;
        }
    }

    pthread_mutex_unlock(&c->lru_m);
}

int rec_append(cache_t *c, record_t *r, const void *buf, size_t n) {
    if(n==0) 
        return 0;
    try_evict_until_soft(c);

    pthread_mutex_lock(&r->m);
    if(r->canceled || r->completed) {
        pthread_mutex_unlock(&r->m);
        return -1;
    }

    size_t need = r->total + n;
    while(need > r->nblocks*BLOCK_SZ) {
        if(r->nblocks == r->capblocks) {
            size_t nc = r->capblocks? r->capblocks*2 : 4;
            block_t **nb = realloc(r->blocks, nc*sizeof(*nb));
            if(!nb) {
                pthread_mutex_unlock(&r->m);
                return -1;
            }

            r->blocks = nb;
            r->capblocks = nc;
        }
        r->blocks[r->nblocks] = calloc(1,sizeof(block_t));
        if(!r->blocks[r->nblocks]) {
            pthread_mutex_unlock(&r->m);
            return -1;
        }

        r->nblocks++;
    }
    size_t off = r->total;
    const unsigned char *p = (const unsigned char*)buf;
    size_t left = n;
    while(left){
        size_t bi = off / BLOCK_SZ;
        size_t bo = off % BLOCK_SZ;
        size_t can = BLOCK_SZ - bo;
        size_t take = left<can?left:can;
        memcpy(r->blocks[bi]->data + bo, p, take);
        size_t u = bo + take;
        
        if(u > r->blocks[bi]->used) 
            r->blocks[bi]->used = u;

        off += take;
        p += take;
        left -= take;
    }
    r->total += n;
    pthread_cond_broadcast(&r->updated);
    pthread_mutex_unlock(&r->m);
    return 0;
}

void rec_finish(cache_t *c, record_t *r) {
    pthread_mutex_lock(&r->m);
    r->completed=1; 
    r->has_fetcher=0;
    pthread_cond_broadcast(&r->updated);
    pthread_mutex_unlock(&r->m);

    pthread_mutex_lock(&c->lru_m);
    if(r->keep_on_complete) {
        c->bytes_completed += r->total;
        lru_push_front(c,r);
        __atomic_add_fetch(&c->stores,1,__ATOMIC_RELAXED);
    }

    pthread_mutex_unlock(&c->lru_m);
}

void rec_cancel(cache_t *c, record_t *r) {
    (void)c;
    pthread_mutex_lock(&r->m);
    r->canceled=1; 
    r->has_fetcher=0;
    pthread_cond_broadcast(&r->updated);
    pthread_mutex_unlock(&r->m);
}

size_t rec_wait_chunk(record_t *r, size_t *off, const void **ptr, size_t *len, int *done, int *canceled){
    *done=0; 
    *canceled=0; 
    *ptr=NULL; 
    *len=0;
    pthread_mutex_lock(&r->m);
    while(1) {
        if(*off < r->total) {
            size_t bi = *off / BLOCK_SZ;
            size_t bo = *off % BLOCK_SZ;
            size_t avail = r->blocks[bi]->used - bo;
            if(avail == 0){ 
                if(bi+1 < r->nblocks){
                    bi++;
                    bo = 0;
                    avail = r->blocks[bi]->used;
                    *off = bi*BLOCK_SZ;
                }
            }
            if(avail > 0){
                *ptr = r->blocks[bi]->data + bo;
                *len = avail;
                break;
            }
        }
        if(r->canceled) {
            *canceled = 1;
            break;
        }
        if(r->completed){
            *done = 1;
            break;
        }
        pthread_cond_wait(&r->updated, &r->m);
    }
    pthread_mutex_unlock(&r->m);
    return *len;
}

const char* rec_key(record_t *r){ return r->key; }
size_t rec_size(record_t *r){ return r->total; }
int rec_is_completed(record_t *r){ return r->completed!=0; }
