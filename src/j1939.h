#ifndef J1939_
#define J1939_

#include <stdint.h>
#include <stdio.h>

typedef uint32_t can_id_t;

typedef struct can_frame_s {
	can_id_t id;
	uint8_t dlc;
	uint8_t data[8];
} can_frame_t;

uint32_t can_id_priority_get(can_id_t id);
uint32_t can_id_pgn_get(can_id_t id);
uint32_t can_id_sa_get(can_id_t id);

void can_id_priority_set(can_id_t id);
void can_id_pgn_set(can_id_t id);
void can_id_sa_set(can_id_t id);

#endif
