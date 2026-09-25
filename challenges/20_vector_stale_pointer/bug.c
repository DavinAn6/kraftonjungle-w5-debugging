/*
 * Challenge 20 — 동적 배열 성장 후 stale 원소 포인터 (심화: 히스토그램 hot 포인터)
 *
 * [시나리오]
 *   키별 빈도를 세는 히스토그램. 버킷들을 동적 배열(Histogram.data)에 담는다.
 *   자주 갱신되는 버킷 하나의 "주소"를 hot 포인터로 캐시해 두고 빠르게 증가시킨다
 *   (인덱스 재조회 없이 hot->count 만 바로 갱신하는 흔한 최적화).
 *
 * [기대 동작]
 *   스트림의 키들을 모두 반영하고, hot 버킷을 갱신한 값과 전체 합을 출력.
 *
 * [증상]
 *   hot 포인터를 캐시한 뒤에도 새로운 키가 계속 들어와 배열이 성장한다. 성장 중
 *   realloc 이 배열을 "다른 주소로 이동"시키면(큰 배열은 mmap 재배치로 옛 영역이
 *   unmap 됨), 이전에 받아둔 hot 은 무효(stale) 주소를 가리킨다. 그 stale 포인터로
 *   hot->count 를 쓰면 매핑 밖 접근 → SIGSEGV.
 *   함정: 저장한 것이 "인덱스"가 아니라 "원소의 주소"라는 점. 성장 후 주소는 바뀐다.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=20_vector_stale_pointer
 *   (gdb) run                        → 크래시(SIGSEGV)
 *   (gdb) bt                         → hot->count += ... 지점
 *   (gdb) print hot                  → 캐시해 둔 옛 주소
 *   (gdb) print h.data               → 성장 후 새 기준 주소(hot 과 범위가 다름)
 *   (gdb) print (char*)hot - (char*)h.data   → hot 이 현재 버퍼 범위 밖임을 확인
 *
 * [printf(로그)로 잡기]
 *   성장 전/후로 data 기준 주소와 hot 을 비교 출력:
 *     fprintf(stderr, "before grow data=%p hot=%p\n", (void*)h.data, (void*)hot);
 *     ... 성장 ...
 *     fprintf(stderr, "after  grow data=%p hot=%p\n", (void*)h.data, (void*)hot);
 *   → 성장 후 data 주소가 바뀌었는데 hot 이 옛 주소 그대로면 stale.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 원소는 "주소"가 아니라 "인덱스"로 참조하세요(성장 후에도 data[idx] 는 유효).
 *       꼭 포인터가 필요하면 realloc(성장) 직후 반드시 다시 계산하세요.
 */
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int  key;
    long count;
} Bucket;

typedef struct {
    Bucket *data;
    size_t  len, cap;
} Histogram;

static void hist_grow(Histogram *h) {
    h->cap = h->cap ? h->cap * 2 : 16;
    Bucket *p = realloc(h->data, h->cap * sizeof(Bucket));   /* 큰 배열은 이동(mmap 재배치) */
    if (!p) { perror("realloc"); free(h->data); exit(1); }
    h->data = p;
}

/* 키를 추가하고, 그 버킷의 주소를 돌려준다(성장이 일어날 수 있음). */
static Bucket *hist_add(Histogram *h, int key) {
    if (h->len == h->cap) hist_grow(h);
    Bucket *b = &h->data[h->len++];
    b->key = key;
    b->count = 0;
    return b;
}
/* 
hist_add(&h, 0)
    Histogram h = { .data = NULL, .len = 0, .cap = 0 };
    hist_grow(h)
            h->cap = 16
            h->data = realloc(h->data, 16 * sizeof(Bucket))
    Bucket b = h->data[0] = { .key = 0, .count = 0 }

hist_add(&h, 1)
    Histogram h = { .data = [b], .len = 1, .cap = 16 };
    Bucket b = h->data[1] = { .key = 1, .count = 0 }

hist_add(&h, 199,999)
*/



static long hist_total(const Histogram *h) {
    long t = 0;
    for (size_t i = 0; i < h->len; i++) t += h->data[i].count;
    return t;
}

int main(void) {
    Histogram h = { .data = NULL, .len = 0, .cap = 0 };
    for (int k = 0; k < 200000; k++) hist_add(&h, k);

    // Bucket *hot = &h.data[100000];  // Crash cause
    // hot->count = 1;

    int hot = 100000;
    h.data[hot].count = 1;

    for (int k = 200000; k < 600000; k++) hist_add(&h, k);
    // h->data has indices 0~599999 with a Bucket that has a key with the same value as index

    h.data[hot].count += 1000;     // ⚠️ Program crashes here
    /* 
    CRASH
        Cannot access memory for Bucket *hot
        p h.data[100000]        {key = 100000, count = 1}
        p &h.data[100000]       (Bucket *) 0x7ffff5f61a10
        p hot                   (Bucket *) 0x7ffff7763a10
    
    CAUSE
        The second for loop called hist_grow which realloced Histogram h to a different memory space
        But hot is still holding the old memory (i.e. stale pointer)
    
    FIX  
        Change from saving pointer to saving just the index num
        Alternatively, could reassign *hot after second for loop
    */

    printf("hot=%ld total=%ld len=%zu\n", h.data[hot].count, hist_total(&h), h.len);
    free(h.data);
    return 0;
}
