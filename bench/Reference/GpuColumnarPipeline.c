#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
  const int64_t n = 20000;
  int64_t *tmp = (int64_t *)malloc((size_t)n * sizeof(int64_t));
  if (!tmp)
    return 1;

  int64_t count = 0;
  for (int64_t x = 1; x <= n; x++) {
    int64_t v = (x + 3) * 2;
    if (v > 20000)
      tmp[count++] = v;
  }

  int64_t sum = 0;
  for (int64_t i = 0; i < count; i++)
    sum += tmp[i];
  free(tmp);
  printf("%lld\n", (long long)sum);
  return 0;
}
