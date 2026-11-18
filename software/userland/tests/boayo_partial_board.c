#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static FILE *in, *out;
static char *reply;
static size_t capacity;
static unsigned sequence;

static void fail(const char *message)
{
    fprintf(stderr, "BOAYO_TEST_ERROR %s\n", message);
    exit(1);
}

static const char *rpc(const char *op, const char *args)
{
    fprintf(out, "{\"id\":%u,\"app\":\"fpga-partial-check\","
                 "\"op\":\"%s\",\"args\":%s}\n", ++sequence, op, args);
    if (fflush(out) || getline(&reply, &capacity, in) < 0)
        fail("RPC connection closed");
    if (!strstr(reply, "\"ok\":true")) {
        fputs(reply, stderr);
        fail(op);
    }
    return reply;
}

static uint64_t field(const char *json, const char *key)
{
    char name[100];
    snprintf(name, sizeof(name), "\"%s\":", key);
    const char *p = strstr(json, name);
    if (!p)
        fail(key);
    return strtoull(p + strlen(name), NULL, 10);
}

static double seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static uint64_t scene_generation(void)
{
    const char *p = strstr(rpc("sync_scene", "{}"), "\"result\":");
    if (!p)
        fail("result missing");
    return strtoull(p + strlen("\"result\":"), NULL, 10);
}

static void presented(const char *label, uint64_t target, double started)
{
    while (seconds() - started < 240) {
        const char *p = rpc("motion_progress", "{}");
        if (strstr(p, "\"phase\":\"failed\"") || !strstr(p, "\"render_error\":null")) {
            fputs(p, stderr);
            fail("render error");
        }
        if (field(p, "rendered_generation") >= target) {
            printf("BOAYO_FRAME %s target=%" PRIu64 " elapsed_ms=%.1f %s",
                   label, target, (seconds() - started)*1000, rpc("profile_progress", "{}"));
            fflush(stdout);
            return;
        }
        nanosleep(&(struct timespec){0, 350000000}, NULL);
    }
    fail(label);
}

static void pixels(unsigned id, int x, int y, int w, int h, int patch)
{
    static const char base64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char *args = malloc((size_t)w*h*4 + 160);
    if (!args)
        fail("allocation");
    char *p = args + sprintf(args, "{\"window_id\":%u,\"x\":%d,\"y\":%d,"
                                  "\"width\":%d,\"height\":%d,\"rgb24\":\"", id,x,y,w,h);
    for (int row = 0; row < h; ++row) {
        for (int col = 0; col < w; ++col) {
            unsigned r = patch ? 240 : 8 + (col * 5 % 230);
            unsigned g = patch ? 24 : 40 + (row * 7 % 215);
            unsigned b = patch ? 24 : 180;
            *p++ = base64[r >> 2];
            *p++ = base64[((r & 3) << 4) | (g >> 4)];
            *p++ = base64[((g & 15) << 2) | (b >> 6)];
            *p++ = base64[b & 63];
        }
    }
    strcpy(p, "\"}");
    rpc("update_surface", args);
    free(args);
}

int main(void)
{
    alarm(1200);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un address = {.sun_family=AF_UNIX,.sun_path="/tmp/bosio-wm.sock"};
    if (fd < 0 || connect(fd, (void *)&address, sizeof(address)))
        fail("connect");
    in = fdopen(fd, "r");
    out = fdopen(dup(fd), "w");
    if (!in || !out)
        fail("open streams");
    if (!strstr(rpc("ping", "{}"), "\"mock_backend\":false"))
        fail("hardware backend required");
    const char *state = rpc("get_state", "{}");
    if (!strstr(state, "\"phase\":\"ready\"") || !strstr(state, "\"windows\":[]")) {
        fputs(state, stderr);
        fail("expected a ready service with an empty scene");
    }

    double started = seconds();
    unsigned id = field(rpc("create_window",
        "{\"title\":\"FPGA patch check\",\"surface_width\":128,\"surface_height\":64,"
        "\"width_deg\":46,\"height_deg\":26,\"azimuth\":-35,\"elevation\":3,"
        "\"input_enabled\":false}"), "window_id");
    pixels(id, 0, 0, 128, 64, 0);
    presented("initial", scene_generation(), started);

    char args[120];
    started = seconds();
    snprintf(args, sizeof(args), "{\"window_id\":%u,\"azimuth\":8}", id);
    rpc("configure_window", args);
    presented("move", scene_generation(), started);

    started = seconds();
    pixels(id, 12, 9, 8, 8, 1);
    uint64_t patched = scene_generation();
    presented("patch", patched, started);

    started = seconds();
    pixels(id, 12, 9, 8, 8, 1);
    if (scene_generation() != patched)
        fail("identical RGB data changed the scene generation");
    presented("same_pixels", patched, started);

    started = seconds();
    snprintf(args, sizeof(args), "{\"window_id\":%u}", id);
    rpc("destroy_window", args);
    presented("cleanup", scene_generation(), started);
    puts("BOAYO_HARDWARE_PARTIAL_OK");
    return 0;
}
