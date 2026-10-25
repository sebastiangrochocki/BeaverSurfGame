/* Frame-budget probe: how many VBlanks do 120 calls of world_step take on a Game Boy Color?
 * The results are printed and also left in globals that tools/probe_gbc.py reads from the
 * linker map while running the ROM in a headless emulator. */
#include <gb/cgb.h>
#include <gb/gb.h>
#include <stdio.h>

#include "../src/rules.h"

Events events;
uint16_t probe_idle, probe_jump, probe_lane, probe_tables, probe_calib, probe_done;
volatile uint16_t sink;

static uint16_t time_steps(uint8_t jumping) {
    uint16_t t0, t1;
    uint8_t i;
    wait_vbl_done();
    t0 = sys_time;
    for (i = 0; i < 120; i++) {
        uint8_t input = INPUT_RIGHT;
        if (jumping && !world.beaver.airborne && (i & 7) == 0) input |= INPUT_JUMP;
        world_step(input, &events);
    }
    t1 = sys_time;
    return (uint16_t)(t1 - t0);
}

/* Double Down, then a jump and a second jump: a hop between lanes and a lane jump. */
static uint16_t time_lanes(void) {
    uint16_t t0, t1;
    uint8_t i;
    wait_vbl_done();
    t0 = sys_time;
    for (i = 0; i < 120; i++) {
        uint8_t input = INPUT_RIGHT;
        uint8_t phase = (uint8_t)(i % 30);
        if (phase == 0 || phase == 2) input |= INPUT_DIVE;
        if (phase == 12 || phase == 18) input |= INPUT_JUMP;
        world_step(input, &events);
    }
    t1 = sys_time;
    return (uint16_t)(t1 - t0);
}

static uint16_t time_tables(void) {
    uint16_t t0, t1;
    uint8_t i;
    uint32_t tick = 0;
    wait_vbl_done();
    t0 = sys_time;
    for (i = 0; i < 120; i++) {
        sea_refresh(tick, 3);
        tick += 7;
    }
    t1 = sys_time;
    return (uint16_t)(t1 - t0);
}

/* Frames for a fixed busy loop; halves when double speed is really on. */
static uint16_t time_calibration(void) {
    uint16_t t0, t1, i;
    wait_vbl_done();
    t0 = sys_time;
    for (i = 0; i < 30000; i++) sink = (uint16_t)(sink + i);
    t1 = sys_time;
    return (uint16_t)(t1 - t0);
}

void main(void) {
    uint16_t slow, fast;
    cgb_compatibility();
    DISPLAY_ON;
    printf("BEAVER PROBE\n\n");
    slow = time_calibration();
    cpu_fast();
    fast = time_calibration();
    probe_calib = (uint16_t)(slow * 16 / (fast ? fast : 1));
    printf("calib %u -> %u\n", slow, fast);
    world_new();
    world_step(INPUT_START, &events);
    probe_idle = time_steps(0);
    probe_jump = time_steps(1);
    probe_lane = time_lanes();
    probe_tables = time_tables();
    probe_done = 1;
    printf("120 steps idle %u\n", probe_idle);
    printf("120 steps jump %u\n", probe_jump);
    printf("120 steps lane %u\n", probe_lane);
    printf("120 refresh    %u\n", probe_tables);
    printf("\nframes per step\nx100: %u / %u\n", (uint16_t)(probe_idle * 100 / 120),
           (uint16_t)(probe_jump * 100 / 120));
    while (1) wait_vbl_done();
}
