/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 파일 서비스(runtime/services/fs) 단위 테스트
 * 수행범위: 루트 밖으로 못 나감(.., 절대 경로, 심볼릭 링크), 보호 경로 거부,
 *           목록(폴더 먼저·이름순, 숨김·링크 제외, 형식), 텍스트·명시적 base64
 *           쓰기·읽기, 텍스트처럼 보이는 base64 문자열은 그대로, 이름 바꾸기·
 *           삭제(루트 자체는 못 지움), 기본 폴더, 건네기(받는 앱 Inbox·시스템 앱이면
 *           Downloads 로 복사, 이름 충돌, 같은 루트는 그대로, 루트 밖·폴더·없는 파일 거부),
 *           한도(읽기 응답은 버스 한도 아래, 쓰기·건네기는 root 예약분을 남기고)
 * 의존방향: runtime/services/fs/fs.c (ZYL_TEST), glib
 * SOLID: SRP — 파일 접근 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-fs-test"
#define ZYL_FILES_ROOT ROOT_ "/files"
#define ZYL_CONTAINERS_ROOT ROOT_ "/appdata"
#include "../runtime/services/fs/fs.c"
#include <assert.h>
#include <unistd.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); ensure_layout(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static bool read_is(const char *rel, const char *want) {
    gsize len = 0;
    char *d = read_file(ZYL_FILES_ROOT, rel, &len);
    bool r = d && len == strlen(want) && memcmp(d, want, len) == 0;
    g_free(d);
    return r;
}

static void test_nothing_outside_the_root(void) {
    assert(symlink("/etc", ZYL_FILES_ROOT "/etc-link") == 0);
    assert(!resolve(ZYL_FILES_ROOT, "../outside") && !resolve(ZYL_FILES_ROOT, "Documents/../../x"));
    assert(!resolve(ZYL_FILES_ROOT, "etc-link/passwd"));               /* a link out of the tree */
    assert(!write_file(ZYL_FILES_ROOT, "etc-link/evil", "x", NULL));
    char *abs = resolve(ZYL_FILES_ROOT, "/Documents/a.txt");            /* a leading / is the root */
    assert(abs && strcmp(abs, ZYL_FILES_ROOT "/Documents/a.txt") == 0);
    g_free(abs);
}

static void test_protected_paths_are_refused(void) {
    assert(!resolve(ZYL_FILES_ROOT, "settings.json") && !resolve(ZYL_FILES_ROOT, ".credentials/key") && !resolve(ZYL_FILES_ROOT, ".SYSTEM/x"));
    assert(!write_file(ZYL_FILES_ROOT, "settings.json", "{}", NULL));
}

static void test_listing_folders_first_without_hidden_or_links(void) {
    assert(write_file(ZYL_FILES_ROOT, "b.txt", "1", NULL) && write_file(ZYL_FILES_ROOT, "A.png", "1", NULL));
    assert(write_file(ZYL_FILES_ROOT, ".hidden", "1", NULL));
    assert(symlink("/etc/passwd", ZYL_FILES_ROOT "/link") == 0);
    GVariant *l = g_variant_ref_sink(list_dir(ZYL_FILES_ROOT, "/"));
    const char *want[] = { "Documents", "Downloads", "Music", "Pictures", "A.png", "b.txt" };
    assert(g_variant_n_children(l) == 6);
    for (gsize i = 0; i < 6; i++) {
        GVariant *e = g_variant_get_child_value(l, i);
        const gchar *name = NULL, *type = NULL;
        g_variant_lookup(e, "name", "&s", &name);
        g_variant_lookup(e, "file_type", "&s", &type);
        assert(strcmp(name, want[i]) == 0);
        if (i == 4) assert(strcmp(type, "image") == 0);
        g_variant_unref(e);
    }
    g_variant_unref(l);
}

static void test_text_and_explicit_base64(void) {
    assert(write_file(ZYL_FILES_ROOT, "Documents/Notes/n.txt", "hello", NULL) && read_is("Documents/Notes/n.txt", "hello"));
    /* Looks like base64 but is text: stays as written (no guessing) */
    const char *token = "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVphYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ejAxMjM0NTY3ODkrL0FCQ0RFRkdISUpLTE1OT1BRUlNU";
    assert(write_file(ZYL_FILES_ROOT, "token.txt", token, NULL) && read_is("token.txt", token));
    assert(write_file(ZYL_FILES_ROOT, "Pictures/p.bin", "AAEC/w==", "base64"));
    gsize len = 0;
    char *bytes = read_file(ZYL_FILES_ROOT, "Pictures/p.bin", &len);
    assert(bytes && len == 4 && (unsigned char)bytes[3] == 0xff);
    g_free(bytes);
}

static void test_rename_and_remove_but_never_the_root(void) {
    assert(write_file(ZYL_FILES_ROOT, "Documents/a.txt", "x", NULL));
    assert(rename_path(ZYL_FILES_ROOT, "Documents/a.txt", "Documents/b.txt") && read_is("Documents/b.txt", "x"));
    assert(!rename_path(ZYL_FILES_ROOT, "Documents/b.txt", "../b.txt"));
    assert(make_dir(ZYL_FILES_ROOT, "Downloads/x/y") && remove_path(ZYL_FILES_ROOT, "Downloads/x"));
    assert(!g_file_test(ZYL_FILES_ROOT "/Downloads/x", G_FILE_TEST_EXISTS));
    assert(!remove_path(ZYL_FILES_ROOT, "/") && !remove_path(ZYL_FILES_ROOT, ""));
    assert(g_file_test(ZYL_FILES_ROOT "/Documents", G_FILE_TEST_IS_DIR));
}

static void test_third_party_app_gets_its_container(void) {
    char *shared = root_for(""), *mine = root_for("com.example.app");
    assert(strcmp(shared, ZYL_FILES_ROOT) == 0);
    assert(strcmp(mine, ZYL_CONTAINERS_ROOT "/com.example.app/files") == 0);
    assert(write_file(mine, "Documents/a.txt", "mine", NULL));
    assert(access(ZYL_CONTAINERS_ROOT "/com.example.app/files/Documents/a.txt", F_OK) == 0);
    assert(access(ZYL_FILES_ROOT "/Documents/a.txt", F_OK) != 0);      /* not the user's files */
    assert(!resolve(mine, "../../../files/Documents"));                 /* no way out */
    assert(!root_for("../files") && !root_for("a/b"));
    g_free(shared);
    g_free(mine);
}

static GVariant *give(const char *from, const char *const *paths, const char *to) {
    return g_variant_ref_sink(handoff_all(from, paths, to));
}

static const char *nth(GVariant *v, gsize i) {
    const char *s = NULL;
    g_variant_get_child(v, i, "&s", &s);
    return s;
}

/* An intent's files: the receiver gets its own copy in its own root */
static void test_handoff_copies_into_the_receivers_root(void) {
    assert(write_file(ZYL_FILES_ROOT, "Pictures/cat.jpg", "meow", NULL));
    const char *one[] = { "Pictures/cat.jpg", NULL };
    GVariant *r = give("", one, "com.example.chat");           /* user files → a third-party app */
    assert(g_variant_n_children(r) == 1 && strcmp(nth(r, 0), "Inbox/cat.jpg") == 0);
    char *chat = root_for("com.example.chat");
    gsize len = 0;
    char *got = read_file(chat, "Inbox/cat.jpg", &len);
    assert(got && len == 4 && memcmp(got, "meow", 4) == 0);
    g_free(got);
    g_variant_unref(r);

    r = give("", one, "com.example.chat");                     /* again: never over the first */
    assert(strcmp(nth(r, 0), "Inbox/cat (1).jpg") == 0);
    g_variant_unref(r);

    assert(write_file(chat, "out/note.txt", "hi", NULL));      /* an app → a system app: Downloads */
    const char *back[] = { "out/note.txt", NULL };
    r = give("com.example.chat", back, "");
    assert(strcmp(nth(r, 0), "Downloads/note.txt") == 0 && read_is("Downloads/note.txt", "hi"));
    g_variant_unref(r);
    g_free(chat);
}

static void test_handoff_same_root_and_refusals(void) {
    assert(write_file(ZYL_FILES_ROOT, "Documents/a.txt", "a", NULL));
    assert(symlink("/etc/passwd", ZYL_FILES_ROOT "/Documents/pw") == 0);
    const char *paths[] = { "Documents/a.txt", "../outside", "Documents", "Documents/missing",
                            "Documents/pw", "settings.json", NULL };
    GVariant *r = give("", paths, "");                         /* system app → system app: as it is */
    assert(strcmp(nth(r, 0), "Documents/a.txt") == 0);
    assert(strcmp(nth(r, 1), "") == 0 && strcmp(nth(r, 5), "") == 0);
    g_variant_unref(r);

    r = give("", paths, "com.example.x");                      /* only a real file inside the root */
    assert(g_variant_n_children(r) == 6 && strcmp(nth(r, 0), "Inbox/a.txt") == 0);
    for (gsize i = 1; i < 6; i++) assert(strcmp(nth(r, i), "") == 0);
    g_variant_unref(r);

    const char *many[] = { "Documents/a.txt", "Documents/a.txt", "Documents/a.txt", "Documents/a.txt",
                           "Documents/a.txt", "Documents/a.txt", "Documents/a.txt", "Documents/a.txt",
                           "Documents/a.txt", "Documents/a.txt", "Documents/a.txt", NULL };
    r = give("", many, "com.example.x");                       /* at most MAX_HANDOFF at once */
    assert(g_variant_n_children(r) == MAX_HANDOFF);
    g_variant_unref(r);
}

/* A reply over the bus limit drops this service's connection and it exits;
   a file too big to send back is "cannot read", not a crash */
static void test_read_reply_stays_under_the_bus_limit(void) {
    char *big = g_strnfill(MAX_REPLY_READ + 1, 'x');
    assert(g_file_set_contents(ZYL_FILES_ROOT "/big.txt", big, -1, NULL));
    gsize len = 0;
    assert(read_file(ZYL_FILES_ROOT, "big.txt", &len) == NULL);
    big[MAX_REPLY_READ] = '\0';
    assert(g_file_set_contents(ZYL_FILES_ROOT "/ok.txt", big, -1, NULL));
    char *d = read_file(ZYL_FILES_ROOT, "ok.txt", &len);
    assert(d && len == MAX_REPLY_READ);
    g_free(d);
    g_free(big);
}

/* An app cannot fill /data to the last block: the root-reserved part and a
   margin stay for the OS — writes and hand-offs past it are refused */
static void test_apps_leave_the_os_its_room(void) {
    assert(write_file(ZYL_FILES_ROOT, "Documents/a.txt", "hello", NULL));
    guint64 saved = keep_free;
    /* as if the disk were full — not the space measured now: tests run side by
       side and another may free some meanwhile */
    keep_free = G_MAXUINT64 / 2;
    assert(!write_file(ZYL_FILES_ROOT, "Documents/b.txt", "hello", NULL));
    assert(!g_file_test(ZYL_FILES_ROOT "/Documents/b.txt", G_FILE_TEST_EXISTS));
    char *got = handoff(ZYL_FILES_ROOT, "Documents/a.txt", ZYL_CONTAINERS_ROOT "/x", "Inbox");
    assert(got == NULL);
    keep_free = saved;
    got = handoff(ZYL_FILES_ROOT, "Documents/a.txt", ZYL_CONTAINERS_ROOT "/x", "Inbox");
    assert(got != NULL);
    g_free(got);
}

int main(void) {
    printf("=== File service ===\n");
    RUN_TEST(test_nothing_outside_the_root);
    RUN_TEST(test_protected_paths_are_refused);
    RUN_TEST(test_listing_folders_first_without_hidden_or_links);
    RUN_TEST(test_text_and_explicit_base64);
    RUN_TEST(test_rename_and_remove_but_never_the_root);
    RUN_TEST(test_third_party_app_gets_its_container);
    RUN_TEST(test_handoff_copies_into_the_receivers_root);
    RUN_TEST(test_handoff_same_root_and_refusals);
    RUN_TEST(test_read_reply_stays_under_the_bus_limit);
    RUN_TEST(test_apps_leave_the_os_its_room);
    assert(system("rm -rf " ROOT_) == 0);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
