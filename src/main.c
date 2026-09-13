#include "modding.h"
#include "ultra64.h"
#include "enums.h"
#include "common_structs.h"

extern Actor *gCurrentActorPointer;
extern PlayerAdditionalActorData *extra_player_info_pointer;
extern u8 cc_player_index;
extern s16 D_global_asm_807FD584;      // Kong index
extern f32 D_global_asm_8075352C[];    // Speed cap
extern s16 D_global_asm_80753548[];    // Deceleration
extern u8 *D_global_asm_807F5AF0;      // Animation script pointer

extern s32 D_global_asm_80767CC0;      // Frame Counter
extern s16 D_global_asm_8075380C[];    // Charge Speed
extern s16 D_global_asm_8075381C[];    // Charge speed cap
extern s16 D_global_asm_8075382C[];    // Charge Deceleration
extern u8  current_character_index[];
extern f32 D_global_asm_807531E0[];    // Run Speed Threshold
extern f32 D_global_asm_807534B8[];    // Crouch Deceleration
extern s16 D_global_asm_807534D4[];    // Crouch turn
extern f32 D_global_asm_807531FC[];    // Kong Walking Maximum Velocity
extern f32 D_global_asm_80753250[];    // Jump Deceleration
extern f32 D_global_asm_807535CC[];    // Jump Y velocity
extern f32 D_global_asm_807535B0[];    // Kong Jumping Y Acceleration
extern f32 D_global_asm_80753594[];    // Jump Y Acceleration (A released)
extern u8  D_global_asm_807F94AF;      // Water contact event
extern u8  D_global_asm_80748E00;      // Skip water surface pin (one frame)
extern u8  D_global_asm_80748E04;      // Pinned to water surface this frame

typedef struct Struct807FD610 {
    s32 unk0; // Timer that ticks up once per frame
    f32 unk4; // Probably float
    f32 unk8; // Probably float
    f32 unkC; // Probably float
    f32 unk10[4];
    s16 unk20[4];
    s16 unk28; // Used
    u16 unk2A; // Used, controller button bitfield
    u16 unk2C; // Used, controller button bitfield
    s8 unk2E; // Used
    s8 unk2F; // Used
    u8 unk30; // Used
    u8 unk31;
    s16 unk32;
} Struct807FD610;
extern Struct807FD610 D_global_asm_807FD610[]; // Often indexed by cc_player_index

extern void getAnimationArg8(u8 *arg0);
extern void getAnimationArg16(s16 *arg0);
extern void func_global_asm_80613AF8(Actor *arg0, s32 arg1, f32 arg2, f32 arg3);
extern void func_global_asm_80613C48(Actor *arg0, s16 arg1, f32 arg2, f32 arg3);
extern void func_global_asm_80614D00(Actor *arg0, f32 arg1, f32 arg2);
extern void func_global_asm_806CFF9C(Actor *arg0);
extern s32  func_global_asm_806E56EC(void);
extern void setYAccelerationFrom80753578(void);
extern void func_global_asm_80685E78(Actor *arg0);
extern void func_global_asm_80686390(Actor *actor, f32 arg1, f32 x, f32 y, f32 z);
extern void func_global_asm_80714950(s32 arg0);
extern s16  playSoundAtPosition(f32 x, f32 y, f32 z, s16 arg3, u8 arg4, s16 arg5, u8 arg6, u8 arg7, f32 arg8, u8 arg9);

extern void func_global_asm_806D3608(void);
extern s32  handleInputsForControlState(s32 arg0);
extern s32  func_global_asm_806725A0(Actor *arg0, s16 arg1);
extern void playAnimation(Actor *arg0, s32 arg2);
extern void func_global_asm_806CBE90(void);
extern void func_global_asm_806CD8EC(void);
extern void func_global_asm_806CD424(s16, f32, f32);
extern void func_global_asm_806CC948(void);
extern void func_global_asm_806CC970(void);
extern void func_global_asm_806CC8A8(void);
extern void func_global_asm_806DF494(s16 *arg0, s16 arg1, s16 arg2);
extern void renderActor(Actor *arg0, u8 arg1);

#define CONTROL_STATE_CHIMPY_CHARGE  0x2E
#define CHARGE_PROGRESS_WINDUP       0
#define CHARGE_PROGRESS_CHARGING     1
#define CHARGE_SCRIPT_INDEX          0x118  // playAnimation(0x48) script for Diddy
#define CHARGE_SKID_ANIM             0xEE   // Skeletal anim the script switches to for the wind-down
#define CHARGE_LOOP_START_OFFSET     0xC9   // Script offset of the first charge-cycle wait
#define CHARGE_WINDDOWN_OFFSET       0x246  // Script offset of the switch to the skid anim
#define CONTROL_STATE_JUMP           0x17
#define CONTROL_STATE_JUMP_DIDDY_2   0x1A   // Second A press in the air (Diddy)
#define CONTROL_STATE_FALL           0x3D
#define RESUME_CHARGE_TIMEOUT        300

#define ACTOR_FLAG_GROUNDED          0x1
#define ACTOR_FLAG_WATER_HERE        0x2
#define ACTOR_FLAG_IN_WATER          0x4

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

static u8  sChargeExtended = 0;   // Charge has looped at least once
static u8  sResumeCharge = 0;     // Left the charge by a jump/fall with B held
static s32 sResumeChargeFrame = 0;
static u8  sChargeHop = 0;        // Airborne inside the charge after an A press
static s32 sChargeHopFrame = 0;
static u8  sChargeOnWater = 0;    // Riding the water surface inside the charge

static int infinite_charge_b_held(void) {
    return (D_global_asm_807FD610[cc_player_index].unk2A & B_BUTTON) != 0;
}

// Charge extension: re-enter the charge straight into its charging phase, no wind-up
static void infinite_charge_resume(Actor *actor) {
    ActorAnimationState *aaS = actor->animation_state;

    extra_player_info_pointer->unk48 = 0x64;
    actor->control_state = CONTROL_STATE_CHIMPY_CHARGE;
    actor->control_state_progress = CHARGE_PROGRESS_CHARGING;
    playAnimation(actor, 0x48);
    extra_player_info_pointer->unk68 = D_global_asm_8075380C[D_global_asm_807FD584] * 4;
    extra_player_info_pointer->unk38 = D_global_asm_8075381C[D_global_asm_807FD584] * 2;
    extra_player_info_pointer->unk30 = D_global_asm_8075382C[D_global_asm_807FD584];

    func_global_asm_80613AF8(actor, 0x46, 0.0f, 1.0f);
    func_global_asm_80614D00(actor, 2.0f, 0.0f);
    aaS->unk6C = (s32)((u8 *)aaS->unk68 + CHARGE_LOOP_START_OFFSET);
    D_global_asm_807F5AF0 = (u8 *)aaS->unk6C;

    actor->unk9C = actor->y_position;
    sChargeExtended = 1;
}

// If a jump/fall that started inside the charge is landing and B is still held, resume the charge
static s32 infinite_charge_try_resume(Actor *actor) {
    u8 airborne;

    if (!sResumeCharge || actor != gCurrentActorPointer) {
        return FALSE;
    }
    sResumeCharge = 0;

    airborne = actor->control_state == CONTROL_STATE_JUMP
            || actor->control_state == CONTROL_STATE_JUMP_DIDDY_2
            || actor->control_state == CONTROL_STATE_FALL;
    if (airborne
        && actor->unk58 == ACTOR_DIDDY
        && (u32)(D_global_asm_80767CC0 - sResumeChargeFrame) < RESUME_CHARGE_TIMEOUT
        && infinite_charge_b_held()) {
        infinite_charge_resume(actor);
        return TRUE;
    }
    return FALSE;
}

// Charge hop: jump-state gravity rules while airborne inside the charge
static void infinite_charge_hop_update(Actor *player) {
    if (!sChargeHop) {
        return;
    }
    D_global_asm_80748E00 = 1;
    if (player->y_velocity < 0.0f) {
        setYAccelerationFrom80753578();
    } else if (!(D_global_asm_807FD610[cc_player_index].unk2A & A_BUTTON)) {
        player->y_acceleration = D_global_asm_80753594[D_global_asm_807FD584];
    }
}

// Water trail with charge: splash when leaving the surface
static void infinite_charge_leave_water(Actor *player) {
    if (sChargeOnWater) {
        func_global_asm_80686390(player, 0.0f, player->x_position, player->unkAC, player->z_position);
        sChargeOnWater = 0;
    }
}

// Ends a hop on touchdown and treats the water surface as ground, with splash and ripple trail
static void infinite_charge_after_physics(Actor *player) {
    u8 onWaterSurface = (player->unk6A & ACTOR_FLAG_WATER_HERE) && player->y_position <= player->unkAC;

    if (sChargeHop) {
        if (player->y_velocity <= 0.0f && D_global_asm_80767CC0 != sChargeHopFrame
            && ((player->unk6A & ACTOR_FLAG_GROUNDED) || onWaterSurface)) {
            sChargeHop = 0;
        } else {
            return;
        }
    }

    if (D_global_asm_80748E04 || onWaterSurface) {
        if (!sChargeOnWater) {
            func_global_asm_80686390(player, 0.0f, player->x_position, player->unkAC, player->z_position);
        } else if ((D_global_asm_80767CC0 & 1) == 0) {
            func_global_asm_80714950(1);
            func_global_asm_80685E78(player);
        }
        sChargeOnWater = 1;

        if ((player->unk6A & ACTOR_FLAG_WATER_HERE) && player->y_position < player->unkAC) {
            player->y_position = player->unkAC;
        }
        player->y_velocity = 0.0f;
        player->unk6A |= ACTOR_FLAG_GROUNDED;
        player->unk6A &= ~ACTOR_FLAG_IN_WATER;
        player->distance_from_floor = 0.0f;
        player->unk9C = player->y_position;
    } else {
        infinite_charge_leave_water(player);
    }
}

// reset on a fresh charge, jump to the wind-down when B is released after extending
static void infinite_charge(Actor *player) {
    ActorAnimationState *aaS = player->animation_state;
    u8 *scriptStart;
    u8 *scriptPos;

    if (!aaS || aaS->unk68 == 0) {
        return;
    }
    scriptStart = (u8 *)aaS->unk68;
    scriptPos   = (u8 *)aaS->unk6C;

    if (player->control_state_progress == CHARGE_PROGRESS_WINDUP) {
        sChargeExtended = 0;
        sResumeCharge = 0;
        sChargeHop = 0;
        sChargeOnWater = 0;
        return;
    }
    if (!sChargeExtended || player->control_state_progress != CHARGE_PROGRESS_CHARGING) {
        return;
    }
    if (infinite_charge_b_held()) {
        return;
    }

    if (aaS->unk64 == CHARGE_SCRIPT_INDEX
        && scriptPos >= scriptStart + CHARGE_LOOP_START_OFFSET
        && scriptPos <= scriptStart + CHARGE_WINDDOWN_OFFSET) {
        aaS->unk6C = (s32)(scriptStart + CHARGE_WINDDOWN_OFFSET);
        aaS->unk78 = 0;
        aaS->unk7C = 0;
    }
    sChargeExtended = 0;
}

RECOMP_PATCH s32 func_global_asm_8061594C(Actor *arg0) {
    s16 sp1E;

    D_global_asm_807F5AF0++;
    getAnimationArg16(&sp1E);
    // mod start: while B is held during the charge, rewind the script to the start of the charge cycle instead of switching to the skid animation.
    if (sp1E == CHARGE_SKID_ANIM
        && arg0 == gCurrentActorPointer
        && arg0->control_state == CONTROL_STATE_CHIMPY_CHARGE
        && arg0->control_state_progress == CHARGE_PROGRESS_CHARGING
        && arg0->animation_state->unk64 == CHARGE_SCRIPT_INDEX
        && infinite_charge_b_held()) {
        D_global_asm_807F5AF0 = (u8 *)arg0->animation_state->unk68 + CHARGE_LOOP_START_OFFSET;
        sChargeExtended = 1;
        return 1;
    }
    // mod end
    func_global_asm_80613C48(arg0, sp1E, 0.0f, 1.0f);
    return 1;
}

RECOMP_PATCH s32 func_global_asm_80617238(Actor *arg0) {
    s16 sp3E;
    u8 sp3D;
    u8 var_v0;
    u8 sp3B;

    sp3B = 0xFF;
    D_global_asm_807F5AF0++;
    getAnimationArg16(&sp3E);
    getAnimationArg8(&sp3D);
    // mod start: skip the sound while the charge is looping so the Woohoo plays only once.
    if (sChargeExtended
        && arg0 == gCurrentActorPointer
        && arg0->control_state == CONTROL_STATE_CHIMPY_CHARGE
        && arg0->control_state_progress == CHARGE_PROGRESS_CHARGING) {
        return 1;
    }
    // mod end
    if (character_change_array[extra_player_info_pointer->unk1A4].unk2C0 == 2) {
        var_v0 = 0xA;
    } else if (character_change_array[extra_player_info_pointer->unk1A4].unk2C0 == 0) {
        var_v0 = 0xFF;
        sp3B = var_v0 * 0.5;
    } else {
        switch (arg0->unk58) {
        case ACTOR_CHUNKY:
        case ACTOR_KRUSHA:
        case ACTOR_RAMBI:
            var_v0 = 0x5A;
            break;
        case ACTOR_DK:
        case ACTOR_LANKY:
            var_v0 = 0x32;
            break;
        default:
            var_v0 = 0x50;
            break;
        }
    }
    playSoundAtPosition(arg0->x_position, arg0->y_position, arg0->z_position, sp3E, (u8) (s32) sp3B, 0x7F, (u8) (s32) sp3D, (u8) var_v0, 0.3f, 0U);
    return 1;
}

RECOMP_PATCH void func_global_asm_806F142C(Actor *arg0) {
    f32 temp_f0;
    f32 temp_f2;

    // mod start: a landing from a jump/fall that started inside the charge resumes the charge.
    if (infinite_charge_try_resume(arg0)) {
        return;
    }
    // mod end
    func_global_asm_806CFF9C(arg0);
    temp_f0 = gCurrentActorPointer->unkB8;
    temp_f2 = temp_f0 / 2;
    gCurrentActorPointer->unkB8 = (D_global_asm_807FD610[cc_player_index].unk8 * (temp_f0 - temp_f2)) + temp_f2;
}

RECOMP_PATCH void func_global_asm_806E4D84(void) {
    // mod start: a held Z on landing resumes the charge instead of crouching.
    if (infinite_charge_try_resume(gCurrentActorPointer)) {
        return;
    }
    // mod end
    if (!func_global_asm_806E56EC()) {
        if (extra_player_info_pointer->unkC8 == -1) {
            if (current_character_index[cc_player_index] != 6 || gCurrentActorPointer->unkB8 < D_global_asm_807531E0[D_global_asm_807FD584]) {
                gCurrentActorPointer->control_state = 0x3C;
                gCurrentActorPointer->control_state_progress = 0;
                playAnimation(gCurrentActorPointer, 0xA);
                extra_player_info_pointer->unk48 = D_global_asm_807534D4[D_global_asm_807FD584];
            }
            extra_player_info_pointer->unk30 = D_global_asm_807534B8[D_global_asm_807FD584];
        }
    }
}

RECOMP_PATCH void func_global_asm_806E1BA4(void) {
    if (D_global_asm_807FD610[cc_player_index].unk2C & A_BUTTON) {
        extra_player_info_pointer->unk58 = D_global_asm_80767CC0;
    }
    if (((D_global_asm_80767CC0 - extra_player_info_pointer->unk58) < 0xFU) && (!(gCurrentActorPointer->unk6A & 1) || (gCurrentActorPointer->unkE0 == 0.0f))) {
        // mod start: inside the charge, A does nothing while airborne (no second jump), and on the ground with B held it launches a hop with the jump physics while staying in the charge.
        if (gCurrentActorPointer->control_state == CONTROL_STATE_CHIMPY_CHARGE
            && gCurrentActorPointer->control_state_progress == CHARGE_PROGRESS_CHARGING) {
            if (!(gCurrentActorPointer->unk6A & 1)) {
                return;
            }
            if (infinite_charge_b_held()) {
                gCurrentActorPointer->y_velocity = D_global_asm_807535CC[D_global_asm_807FD584];
                gCurrentActorPointer->y_acceleration = D_global_asm_807535B0[D_global_asm_807FD584];
                extra_player_info_pointer->unk58 = D_global_asm_80767CC0 - 0x1E;
                sChargeHop = 1;
                sChargeHopFrame = D_global_asm_80767CC0;
                infinite_charge_leave_water(gCurrentActorPointer);
                return;
            }
        }
        // mod end
        extra_player_info_pointer->unk38 = D_global_asm_807531FC[D_global_asm_807FD584];
        gCurrentActorPointer->unkB8 = MIN(gCurrentActorPointer->unkB8, extra_player_info_pointer->unk38);
        extra_player_info_pointer->unk30 = D_global_asm_80753250[D_global_asm_807FD584];
        extra_player_info_pointer->unk2C = 20.0f;
        gCurrentActorPointer->control_state = 0x17;
        gCurrentActorPointer->control_state_progress = 0;
        extra_player_info_pointer->unk54 = D_global_asm_807535CC[D_global_asm_807FD584];
        extra_player_info_pointer->unk50 = 0;
        playAnimation(gCurrentActorPointer, 0x10);
        extra_player_info_pointer->unk58 = D_global_asm_80767CC0 - 0x1E;
    }
}

RECOMP_PATCH u8 func_global_asm_80666AA0(void) {
    // mod start: report no water contact while charging so the charge is never turned into swimming.
    if (gCurrentActorPointer->control_state == CONTROL_STATE_CHIMPY_CHARGE
        && gCurrentActorPointer->control_state_progress <= CHARGE_PROGRESS_CHARGING) {
        return 0;
    }
    // mod end
    return D_global_asm_807F94AF;
}

RECOMP_PATCH void func_global_asm_806D6558(void) {
    func_global_asm_806D3608();
    // mod start: per-frame charge bookkeeping and the B-release wind-down.
    infinite_charge(gCurrentActorPointer);
    // mod end
    switch (gCurrentActorPointer->control_state_progress) {
        case 0:
            extra_player_info_pointer->unk30 = 20.0f;
            func_global_asm_806CD8EC();
            func_global_asm_806CC970();
            break;
        case 1:
            handleInputsForControlState(0x1A);
            // mod start: remember a jump/fall out of the charge with B held so it can resume on landing, and apply the hop gravity rules.
            if ((gCurrentActorPointer->control_state == CONTROL_STATE_JUMP
                 || gCurrentActorPointer->control_state == CONTROL_STATE_FALL)
                && infinite_charge_b_held()) {
                sResumeCharge = 1;
                sResumeChargeFrame = D_global_asm_80767CC0;
                sChargeHop = 0;
            }
            infinite_charge_hop_update(gCurrentActorPointer);
            // mod end
            if (extra_player_info_pointer->unk68 < gCurrentActorPointer->unkB8) {
                extra_player_info_pointer->unk68 = gCurrentActorPointer->unkB8;
            }
            if (gCurrentActorPointer->unkFC != 0) {
                if (D_global_asm_8075352C[D_global_asm_807FD584] < gCurrentActorPointer->unkB8) {
                    gCurrentActorPointer->unkB8 = D_global_asm_8075352C[D_global_asm_807FD584];
                    extra_player_info_pointer->unk68 = D_global_asm_8075352C[D_global_asm_807FD584];
                }
            }
            if ((gCurrentActorPointer->unkFC != 0) && (func_global_asm_806725A0(gCurrentActorPointer, gCurrentActorPointer->y_rotation) == 0)) {
                playAnimation(gCurrentActorPointer, 0x49);
                gCurrentActorPointer->unkEE = (gCurrentActorPointer->y_rotation + 0x800) % 4096;
                gCurrentActorPointer->control_state_progress = 3;
                gCurrentActorPointer->unkB8 = D_global_asm_8075352C[D_global_asm_807FD584];
                extra_player_info_pointer->unk68 = D_global_asm_8075352C[D_global_asm_807FD584];
            } else {
                func_global_asm_806CBE90();
                if (gCurrentActorPointer->unkE0 != 0.0f) {
                    extra_player_info_pointer->unk30 = 20.0f;
                    func_global_asm_806CD8EC();
                } else {
                    func_global_asm_806CD424(gCurrentActorPointer->y_rotation, extra_player_info_pointer->unk68, extra_player_info_pointer->unk38);
                }
                func_global_asm_806CC948();
                // mod start: end a hop on touchdown and ride the water surface as if it were ground.
                infinite_charge_after_physics(gCurrentActorPointer);
                // mod end
                break;
            }
            break;
        case 2:
            handleInputsForControlState(0x1A);
            if ((gCurrentActorPointer->unkFC != 0) && (func_global_asm_806725A0(gCurrentActorPointer, gCurrentActorPointer->y_rotation) == 0)) {
                playAnimation(gCurrentActorPointer, 0x49);
                gCurrentActorPointer->unkEE = (gCurrentActorPointer->y_rotation + 0x800) % 4096;
                gCurrentActorPointer->control_state_progress++;
                gCurrentActorPointer->unkB8 = D_global_asm_8075352C[D_global_asm_807FD584];
                extra_player_info_pointer->unk68 = D_global_asm_8075352C[D_global_asm_807FD584];
            } else {
                func_global_asm_806DF494(&gCurrentActorPointer->y_rotation, gCurrentActorPointer->unkEE, extra_player_info_pointer->unk48);
                func_global_asm_806CBE90();
                func_global_asm_806CD8EC();
                func_global_asm_806CC948();
                extra_player_info_pointer->unk30 = D_global_asm_80753548[D_global_asm_807FD584];
                break;
            }
            break;
        case 3:
            handleInputsForControlState(5);
            gCurrentActorPointer->unkEE = (gCurrentActorPointer->y_rotation + 0x800) % 4096;
            func_global_asm_806CC8A8();
            func_global_asm_806CBE90();
            func_global_asm_806CC948();
            break;
        case 4:
            handleInputsForControlState(5);
            gCurrentActorPointer->unkEE = (gCurrentActorPointer->y_rotation + 0x800) % 4096;
            extra_player_info_pointer->unk30 = 5.0f;
            func_global_asm_806CD8EC();
            func_global_asm_806CC8A8();
            func_global_asm_806CBE90();
            func_global_asm_806CC948();
            break;
    }
    renderActor(gCurrentActorPointer, 0);
}
