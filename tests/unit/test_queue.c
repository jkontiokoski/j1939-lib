/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 jkontiokoski */

#include "unity.h"

#include "j1939/j1939_queue.h"

#define QLEN 4U

static j1939_port_frame_t storage[QLEN];
static j1939_queue_t q;

static void frame(j1939_port_frame_t *f, uint32_t id) {
	const uint8_t payload[1] = {(uint8_t)id};

	j1939_port_frame_build(f, id, payload, 1U);
}

void setUp(void) {
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_init(&q, storage, QLEN));
}

void tearDown(void) {
	/* Every critical section was left and none was nested. */
	TEST_ASSERT_EQUAL_UINT32(0U, q.lock.depth);
	TEST_ASSERT_LESS_OR_EQUAL_UINT32(1U, q.lock.max_depth);
}

static void test_init_rejects_invalid_arguments(void) {
	j1939_queue_t other;

	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_init(NULL, storage, QLEN));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_init(&other, NULL, QLEN));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_init(&other, storage, 0U));
}

static void test_new_queue_is_empty(void) {
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&q));
	TEST_ASSERT_NULL(j1939_queue_peek(&q));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_EMPTY, j1939_queue_pop(&q));
}

static void test_acquire_commit_writes_in_place(void) {
	j1939_port_frame_t *slot = j1939_queue_acquire(&q);

	TEST_ASSERT_EQUAL_PTR(&storage[0], slot);
	frame(slot, 0x100U);
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&q));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_commit(&q));
	TEST_ASSERT_EQUAL_UINT16(1U, j1939_queue_count(&q));
	TEST_ASSERT_EQUAL_PTR(&storage[0], j1939_queue_peek(&q));
	TEST_ASSERT_EQUAL_HEX32(0x100U, j1939_port_frame_id_get(j1939_queue_peek(&q)));
}

static void test_acquire_without_commit_returns_same_slot(void) {
	j1939_port_frame_t *a = j1939_queue_acquire(&q);
	j1939_port_frame_t *b = j1939_queue_acquire(&q);

	TEST_ASSERT_EQUAL_PTR(a, b);
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&q));
}

static void test_fifo_order_and_full_capacity(void) {
	j1939_port_frame_t f;
	uint32_t i;

	for (i = 0U; i < QLEN; i++) {
		frame(&f, i);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&q, &f));
	}
	TEST_ASSERT_EQUAL_UINT16(QLEN, j1939_queue_count(&q));
	TEST_ASSERT_NULL(j1939_queue_acquire(&q));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_queue_put(&q, &f));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_queue_commit(&q));

	for (i = 0U; i < QLEN; i++) {
		TEST_ASSERT_EQUAL_HEX32(i, j1939_port_frame_id_get(j1939_queue_peek(&q)));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(&q));
	}
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(&q));
}

static void test_wraps_around(void) {
	j1939_port_frame_t f;
	uint32_t i;

	/* Three times the capacity, keeping the queue partly filled. */
	frame(&f, 0xAAAU);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&q, &f));
	for (i = 0U; i < (3U * QLEN); i++) {
		frame(&f, i);
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&q, &f));
		TEST_ASSERT_NOT_NULL(j1939_queue_peek(&q));
		TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(&q));
		TEST_ASSERT_EQUAL_UINT16(1U, j1939_queue_count(&q));
	}
	TEST_ASSERT_EQUAL_HEX32(3U * QLEN - 1U, j1939_port_frame_id_get(j1939_queue_peek(&q)));
}

static void test_single_slot_queue(void) {
	j1939_port_frame_t one[1];
	j1939_port_frame_t f;
	j1939_queue_t q1;

	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_init(&q1, one, 1U));
	frame(&f, 1U);
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&q1, &f));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_FULL, j1939_queue_put(&q1, &f));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_pop(&q1));
	TEST_ASSERT_EQUAL(J1939_RET_OK, j1939_queue_put(&q1, &f));
	TEST_ASSERT_EQUAL_UINT16(1U, j1939_queue_count(&q1));
}

static void test_null_queue_is_rejected(void) {
	j1939_port_frame_t f;

	frame(&f, 1U);
	TEST_ASSERT_NULL(j1939_queue_acquire(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_commit(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_put(NULL, &f));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_put(&q, NULL));
	TEST_ASSERT_NULL(j1939_queue_peek(NULL));
	TEST_ASSERT_EQUAL(J1939_RET_ERR_ARG, j1939_queue_pop(NULL));
	TEST_ASSERT_EQUAL_UINT16(0U, j1939_queue_count(NULL));
}

static void test_every_operation_locks(void) {
	j1939_port_frame_t f;
	uint32_t before;

	frame(&f, 1U);
	before = q.lock.count;
	(void)j1939_queue_acquire(&q);
	TEST_ASSERT_EQUAL_UINT32(before + 1U, q.lock.count);
	(void)j1939_queue_commit(&q);
	TEST_ASSERT_EQUAL_UINT32(before + 2U, q.lock.count);
	(void)j1939_queue_peek(&q);
	TEST_ASSERT_EQUAL_UINT32(before + 3U, q.lock.count);
	(void)j1939_queue_pop(&q);
	TEST_ASSERT_EQUAL_UINT32(before + 4U, q.lock.count);
	(void)j1939_queue_count(&q);
	TEST_ASSERT_EQUAL_UINT32(before + 5U, q.lock.count);
}

int main(void) {
	UNITY_BEGIN();
	RUN_TEST(test_init_rejects_invalid_arguments);
	RUN_TEST(test_new_queue_is_empty);
	RUN_TEST(test_acquire_commit_writes_in_place);
	RUN_TEST(test_acquire_without_commit_returns_same_slot);
	RUN_TEST(test_fifo_order_and_full_capacity);
	RUN_TEST(test_wraps_around);
	RUN_TEST(test_single_slot_queue);
	RUN_TEST(test_null_queue_is_rejected);
	RUN_TEST(test_every_operation_locks);
	return UNITY_END();
}
