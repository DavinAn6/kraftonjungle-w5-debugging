/*
 * Challenge 15 — 구조체에 저장된 Dangling Pointer (심화: 세션이 해제된 User 참조)
 *
 * [시나리오]
 *   로그인하면 User 객체를 힙에 만들고, Session 이 그 User 를 가리킨다. User 는 권한
 *   검사 콜백(permission)을 첫 멤버로 가진다. 요청을 처리할 때 세션의 user 를 통해
 *   권한 콜백을 호출한다.
 *
 * [기대 동작]
 *   로그인 → 요청 처리(권한 확인) → 로그아웃 순으로 정상 종료.
 *
 * [증상]
 *   logout() 이 session->user 를 free 하지만 session->user 를 NULL 로 만들지 않는다
 *   (댕글링 멤버). 그 뒤 감사 로그 등에서 같은 크기의 객체를 새로 할당하면 방금 해제된
 *   User 청크가 재사용되어 permission 포인터 자리가 다른 값으로 덮인다.
 *   이후 handle_request() 가 session->user->permission() 을 호출 → 망가진 함수
 *   포인터로 점프 → SIGSEGV/SIGBUS. 크래시는 호출 지점에서 나지만, 원인은
 *   "해제된 객체를 가리키는 구조체 멤버(session->user)를 계속 사용"한 것.
 *
 * [gdb 로 잡기]
 *   make gdb NAME=15_dangling_in_struct
 *   (gdb) run                        → 크래시(SIGSEGV/SIGBUS)
 *   (gdb) bt                         → handle_request 의 s->user->permission(...) 지점
 *   (gdb) print s->user              → 이미 free 된 User 주소(로그아웃에서 해제됨)
 *   (gdb) print s->user->permission  → 재사용으로 오염된(원래 함수와 다른) 포인터
 *   (gdb) break logout               → 언제 user 가 해제되는지 역추적
 *
 * [printf(로그)로 잡기]
 *   free 전/후로 콜백 포인터를 찍어 값이 바뀌는지 확인:
 *     fprintf(stderr, "before logout: perm=%p\n", (void*)s->user->permission);
 *     logout(s);
 *     fprintf(stderr, "after  logout: perm=%p\n", (void*)s->user->permission); // 오염
 *   (stdout 은 버퍼링되니 stderr 로 찍어야 크래시 직전 로그가 남는다)
 *
 * TODO: 객체를 해제하면 그 객체를 가리키던 구조체 멤버도 즉시 무효화(NULL)하고,
 *       사용 전에 NULL 을 검사하세요. "해제 순서 + 소유 포인터 무효화".
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef int (*PermFn)(const char *action);

typedef struct {
    PermFn permission;      
    int    uid;
    char   name[24];
} User;

typedef struct {
    User *user;             
    int   session_id;
} Session;

static int allow_all(const char *action) { (void)action; return 1; }

static User *login(int uid, const char *name) {
    User *u = malloc(sizeof *u);
    if (!u) { perror("malloc"); exit(1); }
    u->permission = allow_all;
    u->uid = uid;
    strncpy(u->name, name, sizeof(u->name) - 1);    // copies 23 bytes. alice\0000...
    u->name[sizeof(u->name) - 1] = '\0';            // adds \0 to last index
    return u;
}
/* LOGIN =================================================================
Function call : login(42, "alice")
    malloc(sizeof *u)
        u->permission = address for the function 'allow_all'
        u->uid = 42
        u->name = "alice\0"

NOTE
u->permission = allow_all; 
    - This is not calling the allow_all function. Just grabbing a reference to it
strncpy(char *dest, const char *src, size_t n)
    - Same as strcpy but n is the max number of characters to copy
    - If src is shorter than n, remaning bytes are filled with \0
*/


static void logout(Session *s) {
    free(s->user);         
    s->user = NULL;
}




/* 감사 로그 항목. User 와 같은 크기라 해제된 청크를 재사용하기 쉽다. */
// Same size as User, so malloc reuses the chunk freed in logout() 
static char *audit_record(const char *event) {
    char *rec = malloc(sizeof(User));       
    if (!rec) exit(1);
    memset(rec, 0xAB, sizeof(User));        /* permission 자리를 0xAB.. 로 오염 */
    // Not just permission. Overwriting entire malloc(sizeof(User)) that was just freed at logout(&s)
    snprintf(rec, sizeof(User), "audit:%s", event); // rec = audit:logout
    return rec;
}

static int handle_request(Session *s, const char *action) {
    // return s->user->permission(action);    // ⚠️ Program crashes here
    if (s != NULL && s->user != NULL) {
        return s->user->permission(action);
    } else {
        return 0;
    }
}



int main(void) {
    Session s;
    s.session_id = 1;
    s.user = login(42, "alice");
    /* 
    p s->user->permission   (PermFn) 0x555555555269 <allow_all>
    p s->user->uid          42
    p s->user->name         "alice", '\000' <repeats 18 times>
    */
    printf("first request allowed=%d\n", handle_request(&s, "read"));   // "first request allowed=1"
    logout(&s);     // free(s->user)

    char *rec = audit_record("logout");      
    printf("%s\n", rec);
    printf("second request allowed=%d\n", handle_request(&s, "write")); // ⚠️ Program crashes here
    /* 
    How is it calling s->user inside handle_request(&s, "write") when we already freed in logout(&s)
        p s->user->permission("write")      SIGSEGV
        p s->user->permission               (PermFn) 0x6f6c3a7469647561
        p s->user                           (User *) 0x5555555592a0
        p s->user->uid                      1953853287
        p s->user->name                     "\000", '\253' <repeats 23 times>
        p rec                               0x5555555592a0 "audit:logout" — same as s->user

    */

    free(rec);
    return 0;
}
