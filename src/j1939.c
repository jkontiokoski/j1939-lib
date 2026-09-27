#include "j1939.h"

uint32_t can_id_priority_get(can_id_t id) {
	return (id & (0x7 << 26));
}

uint32_t can_id_pgn_get(can_id_t id) {
	return (id & (0x12 << 8));
}

uint32_t can_id_sa_get(can_id_t id) {
	return (id & (0xFF));
}

void can_id_priority_set(can_id_t *id) {
	return (id & (0x7 << 26));
}

void can_id_pgn_set(can_id_t *id) {
	return (id & (0x12 << 8));
}

void can_id_sa_set(can_id_t *id) {
	return (id & (0xFF));
}

