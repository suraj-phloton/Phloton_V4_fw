#ifndef TEST_SUPPORT_H
#define TEST_SUPPORT_H
#include <stdio.h>
#include <math.h>
#include <stdint.h>
static unsigned assertions, failures, case_start, failed_start;
static const char *feature, *case_id, *description;
#define EXPECT(expr) do { assertions++; if (!(expr)) { failures++; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
static void begin(const char *f, const char *id, const char *d)
{ feature=f; case_id=id; description=d; case_start=assertions; failed_start=failures; }
static void finish(void)
{
    printf("{\"feature\":\"%s\",\"id\":\"%s\",\"description\":\"%s\",\"status\":\"%s\",\"assertions\":%u}\n",
           feature,case_id,description,failures==failed_start?"PASS":"FAIL",assertions-case_start);
}
#ifdef NEAR
#undef NEAR
#endif
#define NEAR(a,b,e) (fabsf((float)(a)-(float)(b)) <= (e))
#endif
