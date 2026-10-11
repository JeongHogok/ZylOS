/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Infrastructure Layer - Test
 *
 * 역할: 재난문자(Cell Broadcast) 정책 단위 테스트
 * 수행범위: 3GPP 채널 분류, 수신 채널 범위, 재전송 무시(재부팅 후 포함),
 *           업데이트 번호 변경 시 재표시, 기록 상한, 확인 표시 영속
 * 의존방향: runtime/services/telephony/cbs.c, glib
 * SOLID: SRP — 경보 정책 검증만 담당
 * ────────────────────────────────────────────────────────── */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#include "cbs.h"

#define STATE "/tmp/zyl-cbs-test/alerts"

static int tests_run, tests_pass;
#define RUN_TEST(fn) do { tests_run++; printf("  %-56s ", #fn); \
    g_assert_true(system("rm -rf /tmp/zyl-cbs-test") == 0); fn(); tests_pass++; printf("PASS\n"); } while (0)

static void test_channels_map_to_alert_levels(void) {
    assert(zyl_cbs_classify(4370) == ZYL_CBS_PRESIDENTIAL);
    assert(zyl_cbs_classify(4383) == ZYL_CBS_PRESIDENTIAL);   /* additional language */
    assert(zyl_cbs_classify(4371) == ZYL_CBS_EXTREME);
    assert(zyl_cbs_classify(4378) == ZYL_CBS_SEVERE);
    assert(zyl_cbs_classify(4391) == ZYL_CBS_SEVERE);
    assert(zyl_cbs_classify(4379) == ZYL_CBS_AMBER);
    assert(zyl_cbs_classify(4380) == ZYL_CBS_TEST);
    assert(zyl_cbs_classify(4396) == ZYL_CBS_PUBLIC_SAFETY);
    assert(zyl_cbs_classify(4352) == ZYL_CBS_EARTHQUAKE);
    assert(zyl_cbs_classify(4355) == ZYL_CBS_TEST);
    assert(zyl_cbs_classify(4400) == ZYL_CBS_NONE);           /* geo-fencing trigger */
    assert(zyl_cbs_classify(50) == ZYL_CBS_NONE);             /* ordinary cell info */
    assert(strcmp(zyl_cbs_category_name(ZYL_CBS_AMBER), "amber") == 0);
}

static void test_modem_listens_to_every_classified_channel(void) {
    for (unsigned ch = 4096; ch < 4500; ch++) {
        if (zyl_cbs_classify(ch) == ZYL_CBS_NONE) continue;
        int covered = 0;
        for (int i = 0; ZYL_CBS_CHANNELS[i].to; i++)
            if (ch >= ZYL_CBS_CHANNELS[i].from && ch <= ZYL_CBS_CHANNELS[i].to) covered = 1;
        assert(covered);
    }
}

static void test_repeats_are_dropped_updates_are_shown(void) {
    ZylCbsStore *s = zyl_cbs_store_open(NULL);
    assert(zyl_cbs_store_receive(s, 4371, 7, 0, "flood", 1000));
    assert(!zyl_cbs_store_receive(s, 4371, 7, 0, "flood", 1300));       /* network repeat */
    assert(zyl_cbs_store_receive(s, 4371, 7, 1, "flood, updated", 1400)); /* new version */
    assert(zyl_cbs_store_receive(s, 4371, 7, 0, "flood", 1000 + 25 * 3600)); /* a day later */
    assert(!zyl_cbs_store_receive(s, 4400, 1, 0, "", 2000));           /* not an alert */
    assert(zyl_cbs_store_count(s) == 3);
    assert(strcmp(zyl_cbs_store_get(s, 0)->text, "flood") == 0);       /* newest first */
    zyl_cbs_store_free(s);
}

static void test_history_and_acks_survive_restart(void) {
    ZylCbsStore *s = zyl_cbs_store_open(STATE);
    const ZylCbsAlert *a = zyl_cbs_store_receive(s, 4370, 1, 0, "국가 경보", 1000);
    uint64_t id = a->id;
    zyl_cbs_store_receive(s, 4373, 2, 0, "안전 안내", 1001);
    assert(zyl_cbs_store_ack(s, id));
    zyl_cbs_store_free(s);

    s = zyl_cbs_store_open(STATE);
    assert(zyl_cbs_store_count(s) == 2);
    const ZylCbsAlert *old = zyl_cbs_store_get(s, 1);
    assert(old->id == id && old->acked && old->category == ZYL_CBS_PRESIDENTIAL);
    assert(strcmp(old->text, "국가 경보") == 0);
    assert(!zyl_cbs_store_get(s, 0)->acked);
    /* After a reboot the network is still repeating it: no second siren */
    assert(!zyl_cbs_store_receive(s, 4370, 1, 0, "국가 경보", 1500));
    const ZylCbsAlert *next = zyl_cbs_store_receive(s, 4371, 3, 0, "x", 1600);
    assert(next->id > id);                                             /* ids never reused */
    zyl_cbs_store_free(s);
}

static void test_history_keeps_the_newest_fifty(void) {
    ZylCbsStore *s = zyl_cbs_store_open(NULL);
    for (unsigned i = 0; i < ZYL_CBS_HISTORY_MAX + 5; i++)
        assert(zyl_cbs_store_receive(s, 4373, i, 0, "m", 1000 + i));
    assert(zyl_cbs_store_count(s) == ZYL_CBS_HISTORY_MAX);
    assert(zyl_cbs_store_get(s, 0)->code == ZYL_CBS_HISTORY_MAX + 4);
    assert(zyl_cbs_store_get(s, ZYL_CBS_HISTORY_MAX - 1)->code == 5);
    assert(!zyl_cbs_store_ack(s, 9999));
    zyl_cbs_store_free(s);
}

static void test_category_choice_is_respected_and_kept(void) {
    ZylCbsStore *s = zyl_cbs_store_open(STATE);
    assert(!zyl_cbs_store_enabled(s, ZYL_CBS_TEST));                /* off by default */
    assert(!zyl_cbs_store_receive(s, 4380, 1, 0, "monthly test", 1000));
    assert(!zyl_cbs_store_set_enabled(s, ZYL_CBS_PRESIDENTIAL, false));
    assert(zyl_cbs_store_set_enabled(s, ZYL_CBS_SEVERE, false));
    assert(!zyl_cbs_store_receive(s, 4373, 1, 0, "severe", 1000));
    assert(zyl_cbs_store_receive(s, 4370, 1, 0, "national", 1000)); /* always */
    zyl_cbs_store_free(s);
    s = zyl_cbs_store_open(STATE);
    assert(!zyl_cbs_store_enabled(s, ZYL_CBS_SEVERE));
    assert(zyl_cbs_store_enabled(s, ZYL_CBS_PRESIDENTIAL) && zyl_cbs_store_enabled(s, ZYL_CBS_AMBER));
    assert(zyl_cbs_category_from_name("public_safety") == ZYL_CBS_PUBLIC_SAFETY);
    assert(zyl_cbs_category_from_name("nope") == ZYL_CBS_NONE);
    zyl_cbs_store_free(s);
}

int main(void) {
    printf("=== Zyl OS Cell Broadcast Tests ===\n");
    RUN_TEST(test_channels_map_to_alert_levels);
    RUN_TEST(test_modem_listens_to_every_classified_channel);
    RUN_TEST(test_repeats_are_dropped_updates_are_shown);
    RUN_TEST(test_history_and_acks_survive_restart);
    RUN_TEST(test_history_keeps_the_newest_fifty);
    RUN_TEST(test_category_choice_is_respected_and_kept);
    printf("\nResults: %d/%d passed\n", tests_pass, tests_run);
    return tests_pass == tests_run ? 0 : 1;
}
