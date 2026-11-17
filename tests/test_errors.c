/* Original CPU-generated diagnostic fixtures. No device access. */
#include <assert.h>
#include <stdio.h>
#include "f2400-errors.h"
int main(int argc, char **argv) {
    if (argc != 2)
        return 2;
    FILE *input = fopen(argv[1], "r");
    if (!input)
        return 2;
    unsigned a, b, page, mode;
    int expected;
    unsigned cases = 0;
    while (fscanf(input, "%u %u %u %u %d", &a, &b, &page, &mode, &expected) == 5) {
        int actual = f2400_vendor_error(a, b, page, 0, mode);
        if (actual != expected) {
            fprintf(stderr, "Mismatch flags=%02x/%02x page=%02x mode=%02x original=%d linux=%d\n",
                    a, b, page, mode, expected, actual);
            return 1;
        }
        assert(f2400_error_message(actual));
        ++cases;
    }
    if (!feof(input) || !cases)
        return 2;
    fclose(input);
    printf("PASS: %u original error priority/mode/page fixtures; diagnostics never authorize "
           "transport retry.\n",
           cases);
    return 0;
}
