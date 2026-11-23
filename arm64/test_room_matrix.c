#include "MdlPut.h"
#include "camera.h"
#include "cut.h"
#include "main.h"
#include "ps2_NaMath.h"
#include "ps2_NaMatrix.h"
#include "room_engine.h"
#include "ps2/vu0.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The matrix check uses VU0 without starting the engine profiler or VU1.
int prof_enabled;
CAM_WORK cam;
void prof_push(int category) { (void)category; }
void prof_pop(void) {}

void vu1_xgkick(const VuVec *memory, uint32_t quadword, uint32_t mask)
{
    (void)memory; (void)quadword; (void)mask;
    fputs("FAIL: matrix check attempted a VU1 draw\n", stderr);
    exit(1);
}

static void check(int ok, const char *message)
{
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

static void close_float(float actual, float expected)
{
    if (!(fabsf(expected - actual) <= 0.0005f)) {
        fprintf(stderr, "FAIL: original room matrix: %.9g, expected %.9g\n", actual, expected);
        exit(1);
    }
}

static void check_unit_rotation(void)
{
    struct { uint64_t before; NJS_MATRIX matrix; uint64_t after; } work;
    work.before=UINT64_C(0x1122334455667788);work.after=UINT64_C(0x8877665544332211);
    for (unsigned i=0;i<16;++i) work.matrix[i]=(float)(i+1);
    check((uintptr_t)&work.matrix>UINT32_MAX,"rotation test requires a native high address");
    njUnitRotPortion(&work.matrix);
    for (unsigned i=0;i<16;++i)
        check(work.matrix[i]==(i>=12?(float)(i+1):(i==0||i==5||i==10?1.0f:0.0f)),
              "unit rotation changed translation or failed to replace the three axes");
    check(work.before==UINT64_C(0x1122334455667788)&&work.after==UINT64_C(0x8877665544332211),
          "unit rotation crossed its native matrix bounds");
    for (unsigned i=0;i<16;++i) work.matrix[i]=(float)(i+2);
    njSetMatrix(NULL,&work.matrix);njUnitRotPortion(NULL);njGetMatrix(&work.matrix);
    for (unsigned i=0;i<16;++i)
        check(work.matrix[i]==(i>=12?(float)(i+2):(i==0||i==5||i==10?1.0f:0.0f)),
              "unit rotation lost the current native stack pointer");
}

static void check_attachment_multiply(void)
{
    const NJS_MATRIX rotation = {0,1,0,0,-1,0,0,0,0,0,1,0,3,-7,11,1};
    const NJS_MATRIX scaled = {2,0,0,0,0,3,0,0,0,0,4,0,2,4,6,1};
    const NJS_MATRIX expected = {0,2,0,0,-3,0,0,0,0,0,4,0,-1,-5,17,1};
    const NJS_MATRIX squared = {-1,0,0,0,0,-1,0,0,0,0,1,0,10,-4,22,1};
    NJS_MATRIX *operands = calloc(2, sizeof(*operands));
    check(operands && (uintptr_t)operands > UINT32_MAX,
          "attachment matrix test requires high native pointers");
    memcpy(&operands[0], &rotation, sizeof(rotation));
    memcpy(&operands[1], &scaled, sizeof(scaled));
    njMultiMatrix(&operands[0], &operands[1]);
    for (unsigned i = 0; i < 16; ++i) close_float(operands[0][i], expected[i]);
    check(!memcmp(&operands[1], &scaled, sizeof(scaled)), "matrix product changed its source");
    memcpy(&operands[0], &rotation, sizeof(rotation));
    njMultiMatrix(&operands[0], &operands[0]);
    for (unsigned i = 0; i < 16; ++i) close_float(operands[0][i], squared[i]);
    check(njPushMatrixEx(), "attachment matrix stack push failed");
    njSetMatrix(NULL, (NJS_MATRIX *)&rotation);
    njMultiMatrix(NULL, &operands[1]);
    njGetMatrix(&operands[0]);
    for (unsigned i = 0; i < 16; ++i) close_float(operands[0][i], expected[i]);
    check(njPopMatrixEx(), "attachment matrix stack pop failed");
    free(operands);
}

void check_room_matrix(const unsigned char *rdt, size_t bytes, FILE *reference)
{
    RxEngineRoom *room = NULL;
    const char *error = rx_engine_room_load(rdt, bytes, &room);
    check(!error, error);
    ML_WORK *root = &rx_engine_room_rom(room)->mdl;
    size_t count;
    check(fscanf(reference, "%zu", &count) == 1 && count == root->obj_num,
          "missing original room matrix reference");
    NJS_MATRIX *stack = calloc(count + 2, sizeof(*stack));
    root->owP = calloc(count, sizeof(*root->owP));
    check(stack && root->owP && (uintptr_t)stack > UINT32_MAX &&
          (uintptr_t)root->owP > UINT32_MAX, "room matrix test requires high native pointers");

    _Make_SinTable();
    njInitMatrix(stack, (Sint32)count + 1, 0);
    check_attachment_multiply();
    check_unit_rotation();
    njUnitMatrix(NULL);
    njTranslate(NULL, 3, -7, 11);
    NJS_MATRIX before;
    njGetMatrix(&before);
    bhCalcTree(NULL, root);
    NJS_MATRIX after;
    njGetMatrix(&after);
    check(pNaMatMatrixStuckPtr == stack && !memcmp(&before, &after, sizeof(before)),
          "original room traversal did not restore the caller matrix");
    for (size_t i = 0; i < count; ++i) {
        const float *matrix = root->owP[i].mtx;
        for (size_t j = 0; j < 16; ++j) {
            float expected;
            check(fscanf(reference, "%f", &expected) == 1, "truncated room matrix reference");
            close_float(matrix[j], expected);
        }
    }

    // Check original local transform operations and in-place point output.
    check(njPushMatrixEx(), "matrix push failed");
    njUnitMatrix(NULL);
    NJS_VECTOR displacement = {2, 4, 8}, scale = {2, 3, 4};
    Angle angle[3] = {0, 0, 0x4000};
    njTranslateEx(&displacement);
    njRotateEx(angle, 0);
    njScaleEx(&scale);
    NJS_POINT3 point = {1, 0, 1};
    njCalcPoint(NULL, &point, &point);
    close_float(point.x, 2);
    close_float(point.y, 6);
    close_float(point.z, 12);
    check(njPopMatrixEx() && pNaMatMatrixStuckPtr == stack && !njPopMatrixEx(),
          "matrix pop corrupted the caller's stack");

    rom = rx_engine_room_rom(room);
    bhInitCamera();
    int cuts;
    check(fscanf(reference, "%d", &cuts) == 1 && cuts == rom->cut_n, "missing camera pose reference");
    for (int i = 0; i < cuts; ++i) {
        bhSetRenderCut(i);
        bhGetCameraPosition();
        bhMakeCameraVector();
        const CAM_WRK *authored = &rom->cutp[i].cam[0];
        check(cam.ncut == i && cam.pers == authored->pers && cam.ln == authored->ln,
              "original render cut did not select the authored camera");
        int expected_hidden, hidden = 0;
        check(fscanf(reference, "%d", &expected_hidden) == 1, "missing camera visibility reference");
        for (size_t node = 0; node < root->obj_num; ++node)
            hidden += root->objP[node].model != NULL && (root->objP[node].evalflags & 8) != 0;
        check(hidden == expected_hidden, "original render cut applied the wrong visibility mask");
        const float actual[] = {cam.wpx, cam.wpy, cam.wpz, cam.vx, cam.vy, cam.vz};
        for (size_t axis = 0; axis < sizeof(actual) / sizeof(*actual); ++axis) {
            float expected;
            check(fscanf(reference, "%f", &expected) == 1, "truncated original camera pose reference");
            close_float(actual[axis], expected);
        }
    }
    rom = NULL;

    free(root->owP);
    rx_engine_room_destroy(room);
    free(stack);
    printf("PASS original bhCalcTree and native transforms for %zu nodes and %d authored camera cuts\n", count, cuts);
}
