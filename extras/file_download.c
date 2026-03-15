#include <stdio.h>
#include <stdlib.h>
#include <curl/curl.h>

static size_t write_data(void *ptr, size_t size, size_t nmemb, void *userdata) {
    FILE *outfile = (FILE *)userdata;
    size_t written = fwrite(ptr, size, nmemb, outfile);
    return written * size;
}

int main(void) {
    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return 1;
    }

    FILE *outfile = fopen("20250901.as-rel2.txt.bz2", "wb");
    if (outfile == NULL) {
        fprintf(stderr, "Failed to open output file\n");
        curl_easy_cleanup(curl);
        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, "https://publicdata.caida.org/datasets/as-relationships/serial-2/20250901.as-rel2.txt.bz2");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, outfile);

    CURLcode res = curl_easy_perform(curl);
    fclose(outfile);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
        return 1;
    }

    return 0;
}
