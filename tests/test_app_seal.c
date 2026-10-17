/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 앱 봉인(runtime/lib/app_seal.c) 단위 테스트 —
 *       실제 tools/build-store.py 가 만든 봉인을 기기 쪽 판정으로
 * 수행범위: 루트 키 → 서명 키 인증서 → 봉인(2단 키): 루트가 서명하지 않은 서명 키,
 *           폐기된 서명 키(회수 목록, 또는 새 인증서가 앞선 일련번호를 폐기),
 *           스토어 봉인, 파일 변경·추가·삭제, 다른 앱의 봉인, 서명 안 된 봉인,
 *           회수된 앱, 개발 빌드(서명 키가 서명한 인증서·프로필): 허락된 기기·앱
 *           패턴만, 다른 기기·다른 앱·만료·폐기·스토어가 서명하지 않은 인증서는 거부,
 *           내보내는 바이트 확인, 회수 목록 저장·읽기
 * 의존방향: app_seal.c, ota_verify.c, openssl·python3(도구), glib
 * SOLID: SRP — 봉인 규칙 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include "../runtime/lib/app_seal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R "/tmp/zyl-seal-test"
#define TOOL "python3 -I tools/build-store.py"
#define NOW ((gint64)g_get_real_time() / G_USEC_PER_SEC)

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-60s ", #fn); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void sh(const char *cmd) {
    char *full = g_strdup_printf("(%s) >/dev/null 2>&1", cmd);   /* whole command: its own redirections stay */
    if (system(full) != 0) { printf("\n    failed: %s\n", cmd); assert(0); }
    g_free(full);
}

/* Unpack a package as the store would (python's zipfile: the tool's own format) */
static void unpack(const char *zpkg, const char *dir) {
    char *cmd = g_strdup_printf("rm -rf %s && python3 -I -c \"import sys,zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])\" %s %s",
                                dir, zpkg, dir);
    sh(cmd);
    g_free(cmd);
}

static char *root_pem;

static ZylSealVerdict open_as(const char *dir, const char *id, const char *device, const ZylRevocations *rev, gint64 now) {
    return zyl_seal_open(dir, id, "1.0.0", root_pem, device, rev, now, true, NULL);
}

/* What is revoked: comma-separated certificate sha256s, app ids, signing serials */
static ZylRevocations *rev_of(const char *certs, const char *apps, const char *keys) {
    ZylRevocations *r = g_new0(ZylRevocations, 1);
    r->certs = g_strsplit(certs, ",", -1);
    r->apps = g_strsplit(apps, ",", -1);
    r->keys = g_strsplit(keys, ",", -1);
    return r;
}

static void setup(void) {
    sh("rm -rf " R " && mkdir -p " R "/src/hello/js " R "/src/other " R "/out");
    /* The root (offline, in the image) vouches for the store key that signs */
    sh("openssl genrsa -out " R "/root.key 2048 && openssl rsa -in " R "/root.key -pubout -out " R "/root.pub");
    sh("openssl genrsa -out " R "/store.key 2048 && openssl rsa -in " R "/store.key -pubout -out " R "/store.pub");
    sh(TOOL " signing-cert --root-key " R "/root.key --pubkey " R "/store.pub --serial 1 --out " R "/signing.json");
    sh("openssl genrsa -out " R "/dev.key 2048 && openssl rsa -in " R "/dev.key -pubout -out " R "/dev.pub");
    sh("openssl genrsa -out " R "/rogue.key 2048");
    sh("printf '{\"id\":\"com.acme.hello\",\"name\":\"Hello\",\"version\":\"1.0.0\",\"entry\":\"index.html\"}' > " R "/src/hello/app.json"
       " && echo '<p>hi</p>' > " R "/src/hello/index.html && echo 'x=1' > " R "/src/hello/js/app.js");
    sh("printf '{\"id\":\"com.other.app\",\"name\":\"O\",\"version\":\"1.0.0\",\"entry\":\"index.html\"}' > " R "/src/other/app.json"
       " && echo '<p>o</p>' > " R "/src/other/index.html");
    g_file_get_contents(R "/root.pub", &root_pem, NULL, NULL);
}

static void test_store_sealed_app(void) {
    sh(TOOL " package --key " R "/store.key --signing " R "/signing.json " R "/src/hello " R "/out");
    unpack(R "/out/com.acme.hello-1.0.0.zpkg", R "/app");
    ZylSeal *s = NULL;
    assert(zyl_seal_open(R "/app", "com.acme.hello", "1.0.0", root_pem, "", NULL, NOW, true, &s) == ZYL_SEAL_OK);
    assert(zyl_seal_developer(s) == NULL);                                 /* the store's own */
    assert(zyl_seal_bytes_ok(s, "js/app.js", "x=1\n", 4));
    assert(!zyl_seal_bytes_ok(s, "js/app.js", "x=2\n", 4));               /* changed bytes */
    assert(!zyl_seal_bytes_ok(s, ".zyl-seal/seal.json", "", 0));          /* not an app file */
    zyl_seal_free(s);
    assert(open_as(R "/app", "com.other.app", "", NULL, NOW) == ZYL_SEAL_WRONG_APP);
    assert(zyl_seal_open(R "/app", "com.acme.hello", "2.0.0", root_pem, "", NULL, NOW, true, NULL) == ZYL_SEAL_WRONG_APP);
}

/* The store key is the root's word: a seal by a key the root never vouched
   for, or by a signing key revoked since (the kept list, or a newer
   certificate revoking the serial) does not hold */
static void test_the_store_key_is_only_what_the_root_vouches_for(void) {
    /* a signing certificate the root did not sign (the rogue's own) */
    sh(TOOL " signing-cert --root-key " R "/rogue.key --pubkey " R "/store.pub --serial 1 --out " R "/fake.json");
    sh(TOOL " package --key " R "/store.key --signing " R "/fake.json " R "/src/hello " R "/fakeout");
    unpack(R "/fakeout/com.acme.hello-1.0.0.zpkg", R "/fakeapp");
    assert(open_as(R "/fakeapp", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_BAD_SIGNATURE);
    sh("rm -rf " R "/nosigning && cp -r " R "/app " R "/nosigning && rm " R "/nosigning/.zyl-seal/signing.json");
    assert(open_as(R "/nosigning", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_BAD_SIGNATURE);
    /* serial 1 revoked: kept from the store, or by a certificate that revokes it */
    ZylRevocations *r = rev_of("", "", "1");
    assert(open_as(R "/app", "com.acme.hello", "", r, NOW) == ZYL_SEAL_REVOKED);
    zyl_seal_revocations_free(r);
    sh(TOOL " signing-cert --root-key " R "/root.key --pubkey " R "/store.pub --serial 2 --revoke 1 --out " R "/signing2.json");
    char **revokes = NULL;
    sh("mkdir -p " R "/cert2 && cp " R "/signing2.json " R "/cert2/signing.json && cp " R "/signing2.json.sig " R "/cert2/signing.sig");
    char *key = zyl_seal_signing_key(R "/cert2", root_pem, NULL, &revokes);
    assert(key && strstr(key, "BEGIN PUBLIC KEY") && revokes && revokes[0] && strcmp(revokes[0], "1") == 0 && !revokes[1]);
    g_free(key);
    g_strfreev(revokes);
    r = rev_of("", "", "2");                                  /* and it, once revoked itself */
    assert(!zyl_seal_signing_key(R "/cert2", root_pem, r, NULL));
    zyl_seal_revocations_free(r);
    char *store_pub = NULL;
    g_file_get_contents(R "/store.pub", &store_pub, NULL, NULL);
    assert(!zyl_seal_signing_key(R "/cert2", store_pub, NULL, NULL));      /* checked against another key */
    g_free(store_pub);
}

/* An app the store took back does not run, whoever sealed it */
static void test_an_app_the_store_took_back(void) {
    ZylRevocations *r = rev_of("", "com.acme.hello", "");
    assert(open_as(R "/app", "com.acme.hello", "", r, NOW) == ZYL_SEAL_REVOKED);
    zyl_seal_revocations_free(r);
    r = rev_of("", "com.other.app", "");
    assert(open_as(R "/app", "com.acme.hello", "", r, NOW) == ZYL_SEAL_OK);
    zyl_seal_revocations_free(r);
}

static void test_files_must_be_exactly_as_sealed(void) {
    sh("cp -r " R "/app " R "/changed && echo 'evil()' >> " R "/changed/js/app.js");
    assert(open_as(R "/changed", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_FILES_CHANGED);
    sh("rm -rf " R "/added && cp -r " R "/app " R "/added && echo 'evil()' > " R "/added/js/extra.js");
    assert(open_as(R "/added", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_FILES_CHANGED);
    sh("rm -rf " R "/missing && cp -r " R "/app " R "/missing && rm " R "/missing/js/app.js");
    assert(open_as(R "/missing", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_FILES_CHANGED);
    sh("rm -rf " R "/link && cp -r " R "/app " R "/link && ln -s /etc/passwd " R "/link/js/pw");
    assert(open_as(R "/link", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_FILES_CHANGED);
}

static void test_not_signed_by_the_store(void) {
    sh(TOOL " package --key " R "/rogue.key --signing " R "/signing.json " R "/src/hello " R "/rogue");
    unpack(R "/rogue/com.acme.hello-1.0.0.zpkg", R "/rogueapp");
    assert(open_as(R "/rogueapp", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_BAD_SIGNATURE);
    sh("rm -rf " R "/noseal && cp -r " R "/app " R "/noseal && rm -r " R "/noseal/.zyl-seal");
    assert(open_as(R "/noseal", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_MISSING);
}

static void test_development_build_only_where_provisioned(void) {
    sh(TOOL " dev-cert --key " R "/store.key --developer acme --name ACME --pubkey " R "/dev.pub --out " R "/acme.cert.json");
    sh(TOOL " profile --key " R "/store.key --cert " R "/acme.cert.json --app 'com.acme.*' --device DEV-1 --out " R "/acme.profile.json");
    sh(TOOL " package --key " R "/dev.key --signing " R "/signing.json --cert " R "/acme.cert.json --profile " R "/acme.profile.json " R "/src/hello " R "/dev");
    unpack(R "/dev/com.acme.hello-1.0.0.zpkg", R "/devapp");
    ZylSeal *s = NULL;
    assert(zyl_seal_open(R "/devapp", "com.acme.hello", "1.0.0", root_pem, "DEV-1", NULL, NOW, true, &s) == ZYL_SEAL_OK);
    assert(g_strcmp0(zyl_seal_developer(s), "acme") == 0);
    zyl_seal_free(s);
    assert(open_as(R "/devapp", "com.acme.hello", "DEV-2", NULL, NOW) == ZYL_SEAL_NOT_PROVISIONED);  /* another device */
    assert(open_as(R "/devapp", "com.acme.hello", "", NULL, NOW) == ZYL_SEAL_NOT_PROVISIONED);       /* no device ID */
    assert(open_as(R "/devapp", "com.acme.hello", "DEV-1", NULL, NOW + 40 * 86400) == ZYL_SEAL_EXPIRED);
    gchar *fp = NULL;
    sh("sha256sum " R "/acme.cert.json | cut -c1-64 > " R "/fp");
    g_file_get_contents(R "/fp", &fp, NULL, NULL);
    ZylRevocations *r = rev_of(g_strstrip(fp), "", "");
    assert(open_as(R "/devapp", "com.acme.hello", "DEV-1", r, NOW) == ZYL_SEAL_REVOKED);
    zyl_seal_revocations_free(r);
    g_free(fp);

    /* The profile covers com.acme.* only */
    sh(TOOL " package --key " R "/dev.key --signing " R "/signing.json --cert " R "/acme.cert.json --profile " R "/acme.profile.json " R "/src/other " R "/dev2");
    unpack(R "/dev2/com.other.app-1.0.0.zpkg", R "/devother");
    assert(open_as(R "/devother", "com.other.app", "DEV-1", NULL, NOW) == ZYL_SEAL_NOT_PROVISIONED);

    /* A certificate the developer wrote themself: not the store's */
    sh("cp -r " R "/devapp " R "/selfcert && python3 -I -c \"import json,sys; p=sys.argv[1]; d=json.load(open(p)); "
       "d['notAfter']+=999999; open(p,'w').write(json.dumps(d,sort_keys=True,separators=(',',':')))\" " R "/selfcert/.zyl-seal/cert.json");
    assert(open_as(R "/selfcert", "com.acme.hello", "DEV-1", NULL, NOW) == ZYL_SEAL_BAD_SIGNATURE);
}

/* Who signed it decides who may update it: the store's own (NULL) or the
   developer's key from the certificate — the same key, not the same name */
static void test_the_signer_is_the_store_or_the_developers_key(void) {
    assert(zyl_seal_signer_key(R "/app") == NULL);                         /* the store's own */
    char *k = zyl_seal_signer_key(R "/devapp"), *pub = NULL;
    g_file_get_contents(R "/dev.pub", &pub, NULL, NULL);
    assert(k && strstr(pub, "BEGIN PUBLIC KEY") && g_strcmp0(g_strstrip(k), g_strstrip(pub)) == 0);
    g_free(k);
    g_free(pub);
    /* a certificate with no key is no one's — and never the store's */
    sh("rm -rf " R "/nokey && cp -r " R "/devapp " R "/nokey && echo '{}' > " R "/nokey/.zyl-seal/cert.json");
    k = zyl_seal_signer_key(R "/nokey");
    assert(k && strcmp(k, "?") == 0);
    g_free(k);
}

static void test_root_key_and_revocations_kept(void) {
    sh("printf '{\"url\":\"https://s/c.json\",\"rootKey\":\"PEM\"}' > " R "/store.json");
    char *k = zyl_seal_root_key(R "/store.json");
    assert(k && strcmp(k, "PEM") == 0);
    g_free(k);
    assert(!zyl_seal_root_key(R "/none.json"));
    ZylRevocations *r = rev_of("abc", "com.a.b", "1,3");
    r->issued = 1800000000;
    assert(zyl_seal_save_revocations(R "/rev.json", r));
    zyl_seal_revocations_free(r);
    r = zyl_seal_load_revocations(R "/rev.json");
    assert(r->issued == 1800000000 && g_strv_length(r->keys) == 2 && strcmp(r->keys[1], "3") == 0);
    assert(strcmp(r->certs[0], "abc") == 0 && strcmp(r->apps[0], "com.a.b") == 0);
    zyl_seal_revocations_free(r);
    r = zyl_seal_load_revocations(R "/none.json");             /* nothing kept: nothing revoked */
    assert(r->issued == 0 && !r->certs[0] && !r->apps[0] && !r->keys[0]);
    zyl_seal_revocations_free(r);
}

int main(void) {
    printf("=== app seal tests ===\n");
    setup();
    RUN_TEST(test_store_sealed_app);
    RUN_TEST(test_the_store_key_is_only_what_the_root_vouches_for);
    RUN_TEST(test_an_app_the_store_took_back);
    RUN_TEST(test_files_must_be_exactly_as_sealed);
    RUN_TEST(test_not_signed_by_the_store);
    RUN_TEST(test_development_build_only_where_provisioned);
    RUN_TEST(test_the_signer_is_the_store_or_the_developers_key);
    RUN_TEST(test_root_key_and_revocations_kept);
    g_free(root_pem);
    sh("rm -rf " R);
    printf("=== %d/%d passed ===\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
