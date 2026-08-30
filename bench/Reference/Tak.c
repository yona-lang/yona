#include <stdio.h>
#include <stdint.h>
int64_t tak(int64_t x, int64_t y, int64_t z) {
    if (y < x) return tak(tak(x-1,y,z), tak(y-1,z,x), tak(z-1,x,y));
    return z;
}
int main(void) { printf("%ld\n", tak(30, 20, 10)); return 0; }
