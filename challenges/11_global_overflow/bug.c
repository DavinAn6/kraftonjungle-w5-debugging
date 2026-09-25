/*
 * Challenge 11 — Global Buffer Overflow (심화: 전역 아레나 bump 할당기)
 *
 * [시나리오]
 *   고정 크기 전역 버퍼(arena, .bss)를 "bump 포인터" 방식으로 나눠 쓰는 초간단
 *   할당기. 문자열 인터너(intern)가 들어온 문자열을 아레나에 복사해 보관한다.
 *
 * [기대 동작]
 *   문자열을 차례로 아레나에 인터닝하고, 마지막 문자열과 전체 길이 합을 출력한 뒤 정상 종료.
 *
 * [증상]
 *   arena_alloc() 이 남은 공간을 검사하지 않고 offset 만 증가시킨다(bump). 문자열을
 *   계속 인터닝하면 offset 이 ARENA_SIZE 를 넘어, 반환 포인터가 전역 배열 경계 밖을
 *   가리키게 되고 그 위치에 memcpy 로 쓰면서 .bss 를 벗어나 매핑되지 않은 영역까지
 *   침범 → SIGSEGV. 크래시는 intern 의 memcpy 에서 나지만, 원인은 "경계 미검사 bump".
 *
 * [gdb 로 잡기]
 *   make gdb NAME=11_global_overflow
 *   (gdb) run                          → 크래시(SIGSEGV)
 *   (gdb) bt                           → intern 의 memcpy 지점
 *   (gdb) print arena_off              → ARENA_SIZE 를 한참 초과했는지 확인
 *   (gdb) print (long)arena_off - (long)sizeof(arena)   → 경계에서 얼마나 넘었는지
 *   (gdb) print &arena[0]              → 반환 포인터가 arena 범위를 벗어났는지 대조
 *
 * [printf(로그)로 잡기]
 *   할당 때마다 offset 과 용량을 비교 출력:
 *     fprintf(stderr, "alloc n=%zu off=%zu cap=%zu\n", n, arena_off, sizeof(arena));
 *   → off 가 cap 을 넘어서도 계속 커지면 경계를 벗어난 것.
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: arena_alloc() 에서 (arena_off + n <= sizeof(arena)) 를 반드시 검사하고,
 *       공간이 부족하면 NULL 반환 또는 오류 처리하세요.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_SIZE 4096
/* [Thinking Point]
 * 이 arena[] 는 함수 밖에 선언된 '전역 변수'다. 만약 이걸 arena_alloc() 함수 '안'의
 * 지역 변수로 옮기면 무슨 차이가 생길까?
 *   tip 1. 저장 위치가 다르다. 전역/static 은 프로그램 내내 사는 .bss/.data 영역에,
 *          지역 변수는 함수가 실행되는 동안만 사는 '스택'에 놓인다.
 *   tip 2. 수명이 다르다. 전역은 프로그램 시작~끝까지 유지되지만, 지역은 함수가 return
 *          하면 사라진다. → 그 주소를 함수 밖으로 돌려주면 7번(stack use-after-return)!
 *   tip 3. 초기화가 다르다. 전역/static 은 자동으로 0 으로 초기화되지만(그래서 .bss),
 *          지역 변수는 초기화하지 않으면 쓰레기 값이다(8번 챌린지).
 *   생각해보기: 여러 번 호출돼도 같은 저장소를 계속 나눠 쓰려면(커서 arena_off 유지)
 *               이 버퍼는 왜 전역(또는 static)이어야 할까? */
static unsigned char arena[ARENA_SIZE];    /* 전역(.bss) 아레나 */
static size_t arena_off = 0;

static void *arena_alloc(size_t n) {
    void *p = &arena[arena_off];
    arena_off += n;
    return p;
}

static char *intern(const char *s) {
    size_t n = strlen(s) + 1;
    char *dst = arena_alloc(n);
    memcpy(dst, s, n);    // ⚠️ Program crashes here
    return dst;
}
/* INTERN(BUF) ======================================================================
(i = 0) "insert-0"
    n = 9
    arena[4096]
    arena_alloc(9) ———————> *p = &arena[0]
                            p arena[0]      0 '\000'
                            p arena_off     9
    memcpy(dst, s, n) ————> copies n bytes from s to dst
                            p s             0x7fffffffdde0 "insert-0"
                            p dst           0x555555558040 <arena> "insert-0"

Program crashes with SIGSEGV signal at `memcpy(dst, s, n)`
    p s         0x7fffffffdde0 "checkpoint-349"
    p dst       0x7a69c1b6bec7e5b3 <error: Cannot access memory at address 0x7a69c1b6bec7e5b3>
    p n         15

HYPOTHESIS
arena ran out of space? 4096 chars side by side. this was 349th
35 times the entire words array was saved + additional -%d
entire words array is 71 chars + 10 for extra \0 at the end
81*35 = 2835
10(0~9) + 2*90(10~99) + 3*250(100~349) + 350(for the dash mark) = 1290
Both combined is 4125, over 4096
After "compact-347" is added, arena_off is 4096. Can't add "serialize-348"
*/





int main(void) {
    const char *words[] = {
        "insert", "delete", "search", "traverse", "balance",
        "rotate", "rehash", "compact", "serialize", "checkpoint",
    };
    int nwords = (int)(sizeof(words) / sizeof(words[0]));
    /* NWORDS =======================================================================
    words has 10 pointers, so sizeof(words) = 80 bytes
    one pointer has 8 bytes, so sizeof(words[0]) = 8 bytes
    nwords = number of elements in words = 10
    */
    

    char *last = NULL;
    long total = 0;
    for (int i = 0; i < 100000; i++) {
        char buf[32];
        snprintf(buf, sizeof buf, "%s-%d", words[i % nwords], i);
        /* SNPRINTF =================================================================
        fill each new buf with "words[i % 10] - i" within size limitation
        (i = 0)     "insert-0"
        (i = 1)     "delete-1"
        ...
        (i = 9)     "checkpoint-9"
        (i = 10)    "insert-10"
        (i = 11)    "delete-11"
        */
        if (arena_off + strlen(buf) + 1 > ARENA_SIZE) break; // Fix to check arena capacity
        last = intern(buf);             // ⚠️ Program crashes from intern function             
        total += (long)strlen(last);
    }

    printf("interned, last=%s total_len=%ld\n", last, total);
    return 0;
}
