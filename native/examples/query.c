#include "pluscode_admin/pluscode_admin.h"
#include <stdio.h>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    pcad_handle handle = 0;
    char* result = NULL;
    char* error = NULL;
    pcad_status status = pcad_open(argv[1], NULL, &handle, &error);
    if (status == PCAD_OK) {
        status = pcad_lookup_latlng(handle, 39.9042, 116.4074, &result, &error);
        if (status == PCAD_OK) puts(result);
    }
    if (status != PCAD_OK) fprintf(stderr, "%s\n", error ? error : "Native error");
    pcad_free(result);
    pcad_free(error);
    if (handle) pcad_close(handle, NULL);
    return status == PCAD_OK ? 0 : 1;
}
