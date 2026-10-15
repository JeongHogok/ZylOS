/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 파일 서비스(runtime/services/fs) 단위 테스트
 * 수행범위: 루트 밖으로 못 나감(.., 절대 경로, 심볼릭 링크), 보호 경로 거부,
 *           목록(폴더 먼저·이름순, 숨김·링크 제외, 형식), 텍스트·명시적 base64
 *           쓰기·읽기, 텍스트처럼 보이는 base64 문자열은 그대로, 이름 바꾸기·
 *           삭제(루트 자체는 못 지움), 기본 폴더
 * 의존방향: runtime/services/fs/fs.c (ZYL_TEST), glib
 * SOLID: SRP — 파일 접근 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#define ROOT_ "/tmp/zyl-fs-test"
#define ZYL_FILES_ROOT ROOT_ "/files"
#include "../runtime/services/fs/fs.c"
#include <assert.h>

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); assert(system("rm -rf " ROOT_) == 0); ensure_layout(); fn(); tests_pass++; printf("PASS\n"); } while (0)

static bool read_is(const char *rel, const char *want) {
    gsize len = 0;
    char *d = read_file(rel, &len);
    bool r = d && len == strlen(want) && memcmp(d, want, len) == 0;
    g_free(d);
    return r;
}

static void test_nothing_outside_the_root(void) {
    assert(symlink("/etc", ZYL_FILES_ROOT "/etc-link") == 0);
    assert(!resolve("../outside") && !resolve("Documents/../../x"));
    assert(!resolve("etc-link/passwd"));               /* a link out of the tree */
    assert(!write_file("etc-link/evil", "x", NULL));
    char *abs = resolve("/Documents/a.txt");            /* a leading / is the root */
    assert(abs && strcmp(abs, ZYL_FILES_ROOT "/Documents/a.txt") == 0);
    g_free(abs);
}

static void test_protected_paths_are_refused(void) {
    assert(!resolve("settings.json") && !resolve(".credentials/key") && !resolve(".SYSTEM/x"));
    assert(!write_file("settings.json", "{}", NULL));
}

static void test_listing_folders_first_without_hidden_or_links(void) {
    assert(write_file("b.txt", "1", NULL) && write_file("A.png", "1", NULL));
    assert(write_file(".hidden", "1", NULL));
    assert(symlink("/etc/passwd", ZYL_FILES_ROOT "/link") == 0);
    GVariant *l = g_variant_ref_sink(list_dir("/"));
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
    assert(write_file("Documents/Notes/n.txt", "hello", NULL) && read_is("Documents/Notes/n.txt", "hello"));
    /* Looks like base64 but is text: stays as written (no guessing) */
    const char *token = "QUJDREVGR0hJSktMTU5PUFFSU1RVVldYWVphYmNkZWZnaGlqa2xtbm9wcXJzdHV2d3h5ejAxMjM0NTY3ODkrL0FCQ0RFRkdISUpLTE1OT1BRUlNU";
    assert(write_file("token.txt", token, NULL) && read_is("token.txt", token));
    assert(write_file("Pictures/p.bin", "AAEC/w==", "base64"));
    gsize len = 0;
    char *bytes = read_file("Pictures/p.bin", &len);
    assert(bytes && len == 4 && (unsigned char)bytes[3] == 0xff);
    g_free(bytes);
}

static void test_rename_and_remove_but_never_the_root(void) {
    assert(write_file("Documents/a.txt", "x", NULL));
    assert(rename_path("Documents/a.txt", "Documents/b.txt") && read_is("Documents/b.txt", "x"));
    assert(!rename_path("Documents/b.txt", "../b.txt"));
    assert(make_dir("Downloads/x/y") && remove_path("Downloads/x"));
    assert(!g_file_test(ZYL_FILES_ROOT "/Downloads/x", G_FILE_TEST_EXISTS));
    assert(!remove_path("/") && !remove_path(""));
    assert(g_file_test(ZYL_FILES_ROOT "/Documents", G_FILE_TEST_IS_DIR));
}

int main(void) {
    printf("=== File service ===\n");
    RUN_TEST(test_nothing_outside_the_root);
    RUN_TEST(test_protected_paths_are_refused);
    RUN_TEST(test_listing_folders_first_without_hidden_or_links);
    RUN_TEST(test_text_and_explicit_base64);
    RUN_TEST(test_rename_and_remove_but_never_the_root);
    assert(system("rm -rf " ROOT_) == 0);
    printf("Results: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
