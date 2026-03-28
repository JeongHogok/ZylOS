/* ──────────────────────────────────────────────────────────
 * [Clean Architecture] Domain Layer - Interface
 *
 * 역할: 디스플레이 출력 라이프사이클 관리 인터페이스 정의
 * 수행범위: output_register_listeners 등 출력 관련 함수 선언
 * 의존방향: bpi_compositor.h
 * SOLID: ISP — 출력 관련 인터페이스만 노출
 * ────────────────────────────────────────────────────────── */

#ifndef BPI_OUTPUT_OUTPUT_H
#define BPI_OUTPUT_OUTPUT_H

#include "bpi_compositor.h"

/*
 * Register the new_output listener on the backend so that displays
 * are set up automatically when they appear.
 */
void output_register_listeners(struct bpi_server *server);

#endif /* BPI_OUTPUT_OUTPUT_H */
