#include <stdio.h>
#include <stdlib.h>

int main(void) {
    int result = system("bunzip2 -f 20250901.as-rel2.txt.bz2");
    if (result != 0) {
        fprintf(stderr, "Failed to decompress\n");
        return 1;
    }
    return 0;
}
