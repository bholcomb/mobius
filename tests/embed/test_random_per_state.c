/* Each state has its own random generator. random() used the C library's
 * rand(), shared by the whole process: a script's randomseed reseeded the
 * host's rand(), and states disturbed each other's sequences. */
#include <mobius/mobius.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(cond, what) do { if (!(cond)) { printf("FAIL: %s\n", what); return 1; } } while (0)

int main(void) {
    srand(1);
    int expected[3];
    for (int i = 0; i < 3; i++) expected[i] = rand();
    srand(1);
    int got0 = rand();

    MobiusState* a = mobius_new_state(NULL);
    MobiusState* b = mobius_new_state(NULL);
    CHECK(a && b, "create states");
    mobius_init_stdlib(a);
    mobius_init_stdlib(b);

    /* The script seeds and draws; the host's rand() sequence goes on. */
    CHECK(mobius_exec_string(a, "randomseed(5)\nvar x = random(100)") == MOBIUS_OK, "a draws");
    int got1 = rand(), got2 = rand();
    CHECK(got0 == expected[0] && got1 == expected[1] && got2 == expected[2],
          "host rand() unaffected by script randomseed");

    /* Same seed, same sequence, even with another state drawing between. */
    CHECK(mobius_exec_string(a, "randomseed(99)\nvar s1 = [random(1000), random(1000), random(1000)]") == MOBIUS_OK, "a seq");
    CHECK(mobius_exec_string(b, "randomseed(99)\nvar first = random(1000)") == MOBIUS_OK, "b first");
    CHECK(mobius_exec_string(a, "randomseed(12345)\nfor (var i = 0; i < 100; i++) { random() }") == MOBIUS_OK, "a churns");
    CHECK(mobius_exec_string(b, "var rest = [random(1000), random(1000)]") == MOBIUS_OK, "b rest");
    CHECK(mobius_exec_string(a, "var a_first = s1[0]\nvar a_rest = [s1[1], s1[2]]") == MOBIUS_OK, "a copy");
    /* Compare across states through their printed values. */
    CHECK(mobius_exec_string(b, "randomseed(99)\nvar again = [random(1000), random(1000), random(1000)]\n"
                                "if (again[0] != first || again[1] != rest[0] || again[2] != rest[1]) { throw \"b sequence disturbed\" }") == MOBIUS_OK,
          "b's sequence independent of a");

    mobius_free_state(a);
    mobius_free_state(b);
    printf("random per state: PASS\n");
    return 0;
}
