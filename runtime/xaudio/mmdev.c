/* The MMDevice objects and the enumerator's notification table (runtime/include/rt/mmdev.h).
 * The method bodies are in runtime/xaudio/IMMDevice*, IMMDeviceEnumerator. */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "rt/log.h"
#include "rt/mmdev.h"

#define MM_CB_MAX 8

/* One list per enumerator object, keyed by the object's state address. */
typedef struct MmCbList {
    const void *key;
    const void *cb[MM_CB_MAX];
    unsigned    n;
    struct MmCbList *next;
} MmCbList;

static MmCbList *cb_lists;
static pthread_mutex_t cb_lock = PTHREAD_MUTEX_INITIALIZER;

static RtComIface *const mm_device_faces[] = { &rt_iface_IMMDevice };
static RtComIface *const mm_collection_faces[] = { &rt_iface_IMMDeviceCollection };
const RtComClass rt_mm_device_class = { "MMDevice", mm_device_faces, 1, sizeof(MmDevice), NULL, NULL };
const RtComClass rt_mm_collection_class = { "MMDeviceCollection", mm_collection_faces, 1, sizeof(MmCollection), NULL, NULL };

bool rt_mm_id_is_endpoint(const void *guest_wstr) {
    const uint16_t *p = guest_wstr;
    const char *want = MM_ENDPOINT_ID;
    if (!p) return false;
    for (; *want; want++, p++)
        if (*p != (uint16_t)(unsigned char)*want) return false;
    return *p == 0;
}

void *rt_mm_device_new(void) {
    return rt_com_new(&rt_mm_device_class);
}

void *rt_mm_collection_new(unsigned count) {
    void *obj = rt_com_new(&rt_mm_collection_class);
    MmCollection *c = rt_com_state_of(obj, &rt_mm_collection_class);
    c->count = count;
    return obj;
}

/* Caller holds cb_lock. */
static MmCbList *cb_find(const void *key) {
    for (MmCbList *l = cb_lists; l; l = l->next)
        if (l->key == key) return l;
    return NULL;
}

bool rt_mm_callbacks_add(const void *key, const void *cb) {
    pthread_mutex_lock(&cb_lock);
    MmCbList *l = cb_find(key);
    if (!l) {
        l = calloc(1, sizeof *l);
        if (!l) rt_fatal("rt_mm: no host memory for an enumerator's notification list");
        l->key = key;
        l->next = cb_lists;
        cb_lists = l;
    }
    bool ok = l->n < MM_CB_MAX;
    if (ok) l->cb[l->n++] = cb;
    pthread_mutex_unlock(&cb_lock);
    return ok;
}

bool rt_mm_callbacks_remove(const void *key, const void *cb) {
    bool found = false;
    pthread_mutex_lock(&cb_lock);
    MmCbList *l = cb_find(key);
    for (unsigned i = 0; l && i < l->n && !found; i++) {
        if (l->cb[i] != cb) continue;
        for (unsigned j = i + 1; j < l->n; j++) l->cb[j - 1] = l->cb[j];
        l->n--;
        found = true;
    }
    pthread_mutex_unlock(&cb_lock);
    return found;
}

void rt_mm_enumerator_destroy(void *key) {
    pthread_mutex_lock(&cb_lock);
    for (MmCbList **pp = &cb_lists; *pp; pp = &(*pp)->next) {
        if ((*pp)->key != key) continue;
        MmCbList *dead = *pp;
        *pp = dead->next;
        free(dead);
        break;
    }
    pthread_mutex_unlock(&cb_lock);
}
