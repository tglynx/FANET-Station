//
// Created by Reinart, Manfred on 08.10.19.
//

//#define JSMN_HEADER
#include "jsmn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <curl/easy.h>

#include "fanet_struct.c"

const char *holfuyLiveApiUrlFormat = "http://api.holfuy.com/live/?s=%s&pw=%s";
const char *holfuyUrlFormatJson = "http://api.holfuy.com/live/?s=%s&su=km/h&m=JSON&loc&pw=%s";
const char *holfuyPugetUrlFormat = "https://holfuy.com/puget/mjso.php?k=%s";    // flat JSON
const char *windsMobyUrlFormat = "https://winds.mobi/api/2/stations/?ids=%s";

struct MemoryStruct {
    char *memory;
    size_t size;
};

// Function to remove all spaces from a given string
// https://www.geeksforgeeks.org/remove-spaces-from-a-given-string/
void removeSpaces(char *str)
{
    // To keep track of non-space character count
    int count = 0;

    // Traverse the given string. If current character
    // is not space, then place it at index 'count++'
    for (int i = 0; str[i]; i++)
        if (str[i] != ' ')
            str[count++] = str[i]; // here count is
    // incremented
    str[count] = '\0';
}

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if(ptr == NULL) {
        /* out of memory! */
        printf("not enough memory (realloc returned NULL)\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

static int jsoneq(const char *json, jsmntok_t *tok, const char *s) {
    if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
        strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
        return 0;
    }
    return -1;
}

int get_winds_mobi_json(const char* station_id, sWeather *_weather) {
    char url[72];
    sprintf(url, windsMobyUrlFormat, station_id);
    printf(" - fetching JSON from URL: %s", url);

    CURL *curl_handle;
    CURLcode res;
    int error = 0;
    struct MemoryStruct hf_chunk;
    hf_chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    hf_chunk.size = 0;    /* no data at this point */
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&hf_chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    } else {
        printf(" : %s\n", hf_chunk.memory);

        jsmn_parser p;
        jsmntok_t json_tokens[128]; /* We expect no more than 128 JSON tokens */

        jsmn_init(&p);
        char *json_string = hf_chunk.memory;
        char *token_string;
        int r = jsmn_parse(&p, json_string, strlen(json_string), json_tokens, 128);

        _weather->time = 0; // default with current time?

        _weather->wind = false;
        _weather->wind_speed = 0.0;
        _weather->wind_gusts = 0.0;
        _weather->wind_heading = 0.0;

        _weather->temp = false;
        _weather->temperature = 0.0;

        _weather->humid = false;
        _weather->humidity = 0.0;
        _weather->barom = false;
        _weather->barometric = 0.0;

        for (int i = 1; i < r; i++) {
            if (jsoneq(json_string, &json_tokens[i], "w-dir") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_heading = atof(token_string);
                _weather->wind = true;
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "w-avg") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_speed = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "w-max") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_gusts = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "temp") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->temperature = atof(token_string);
                _weather->temp = true;
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "_id") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                if (strlen(_weather->id_station) == 0) {
                    sprintf(_weather->id_station, "%s", token_string);
                } else {
                    _weather->time = atoi(token_string);
                }
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "name") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                sprintf(_weather->name, "%s", token_string);
                token_string = strndup((json_string + json_tokens[i + 1].start), 4);
                sprintf(_weather->short_name, "%s", token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "coordinates") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                if (json_tokens[i + 1].type != JSMN_ARRAY) {
                    continue; /* We expect groups to be an array of strings */
                }
                for (int j = 0; j < json_tokens[i + 1].size; j++) {
                    jsmntok_t *g = &json_tokens[i + j + 2];
                    printf("  * %.*s\n", g->end - g->start, json_string + g->start);
                }
                jsmntok_t *g = &json_tokens[i + 0 + 2];
                token_string = strndup((json_string + g->start),g->end - g->start);
                _weather->longitude = atof(token_string);
                g = &json_tokens[i + 1 + 2];
                token_string = strndup((json_string + g->start),g->end - g->start);
                _weather->latitude = atof(token_string);
                i += json_tokens[i + 1].size + 1;
//                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "alt") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->altitude = atoi(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "dateTime") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                char time_str[25];
                sprintf(time_str, "%s", token_string);
                struct tm tm;
                strftime(time_str, 100, "%Y-%m-%d %H:%M:%S", &tm);
                _weather->time = mktime(&tm);
                i++;
            } else {
//                printf("----- Unexpected key: %.*s\n", json_tokens[i].end - json_tokens[i].start,
//                       json_string + json_tokens[i].start);
            }
        }

    }
    curl_easy_cleanup(curl_handle);
    free(hf_chunk.memory);
    curl_global_cleanup();
    return error;
}

int get_holfuy_weather_json(const char* holfuy_id, const char* token, sWeather *_weather) {

    char url[72];
    sprintf(url, holfuyUrlFormatJson, holfuy_id, token);
//    printf(" - fetching JSON from URL: %s", url);

    CURL *curl_handle;
    CURLcode res;
    int error = 0;
    struct MemoryStruct hf_chunk;
    hf_chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    hf_chunk.size = 0;    /* no data at this point */
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&hf_chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    } else {
//        printf(" : %s\n", hf_chunk.memory);

        jsmn_parser p;
        jsmntok_t json_tokens[128]; /* We expect no more than 128 JSON tokens */

        jsmn_init(&p);
        char *json_string = hf_chunk.memory;
        char *token_string;
        int r = jsmn_parse(&p, json_string, strlen(json_string), json_tokens, 128);

        _weather->time = 0; // default with current time?

        _weather->wind = false;
        _weather->wind_speed = 0.0;
        _weather->wind_gusts = 0.0;
        _weather->wind_heading = 0.0;

        _weather->temp = false;
        _weather->temperature = 0.0;

        _weather->humid = false;
        _weather->humidity = 0.0;
        _weather->barom = false;
        _weather->barometric = 0.0;

        for (int i = 1; i < r; i++) {
            if (jsoneq(json_string, &json_tokens[i], "direction") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_heading = atof(token_string);
                _weather->wind = true;
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "speed") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_speed = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "gust") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_gusts = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "temperature") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->temperature = atof(token_string);
                _weather->temp = true;
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "stationId") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                sprintf(_weather->id_station, "%s", token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "stationName") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                sprintf(_weather->name, "%s", token_string);
                token_string = strndup((json_string + json_tokens[i + 1].start), 4);
                sprintf(_weather->short_name, "%s", token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "latitude") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->latitude = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "longitude") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->longitude = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "altitude") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->altitude = atoi(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "dateTime") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                char time_str[25];
                sprintf(time_str, "%s", token_string);
                struct tm tm;
                strftime(time_str, 100, "%Y-%m-%d %H:%M:%S", &tm);
                _weather->time = mktime(&tm);
                i++;
            } else {
//                printf("----- Unexpected key: %.*s\n", json_tokens[i].end - json_tokens[i].start,
//                       json_string + json_tokens[i].start);
            }
        }

    }
    curl_easy_cleanup(curl_handle);
//    free(hf_chunk.memory);
    curl_global_cleanup();
    return error;
}

int get_holfuy_puget_json(const char* holfuy_id, const char* token, sWeather *_weather) {

    char url[72];
    sprintf(url, holfuyPugetUrlFormat, holfuy_id);
    printf(" - fetching JSON from URL: %s", url);

    CURL *curl_handle;
    CURLcode res;
    int error = 0;
    struct MemoryStruct hf_chunk;
    hf_chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    hf_chunk.size = 0;    /* no data at this point */
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&hf_chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    } else {
        printf(" : %s\n", hf_chunk.memory);

//        char *parsed[20];
//        int numToken = 0;

        jsmn_parser p;
        jsmntok_t json_tokens[128]; /* We expect no more than 128 JSON tokens */

        jsmn_init(&p);
        char *json_string = hf_chunk.memory;
        char *token_string;
        int r = jsmn_parse(&p, json_string, strlen(json_string), json_tokens, 128);

        _weather->time = 0;

        _weather->wind = false;
        _weather->wind_speed = 0.0;
        _weather->wind_gusts = 0.0;
        _weather->wind_heading = 0.0;

        _weather->temp = false;
        _weather->temperature = 0.0;

        _weather->humid = false;
        _weather->humidity = 0.0;
        _weather->barom = false;
        _weather->barometric = 0.0;

        for (int i = 1; i < r; i++) {
            if (jsoneq(json_string, &json_tokens[i], "dir") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_heading = atof(token_string);
                _weather->wind = false;
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "speed") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_speed = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "gust") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->wind_gusts = atof(token_string);
                i++;
            } else if (jsoneq(json_string, &json_tokens[i], "temperature") == 0) {
                token_string = strndup((json_string + json_tokens[i + 1].start),
                                       (json_tokens[i + 1].end - json_tokens[i + 1].start));
                _weather->temperature = atof(token_string);
                _weather->temp = true;
                i++;
            } else {
//                printf("----- Unexpected key: %.*s\n", json_tokens[i].end - json_tokens[i].start,
//                       json_string + json_tokens[i].start);
            }
        }

    }
    curl_easy_cleanup(curl_handle);
    free(hf_chunk.memory);
    curl_global_cleanup();
    return error;
}

int get_holfuy_weather(const char* holfuy_id, const char* token, sWeather *_weather) {
    char url[72];
    sprintf(url, holfuyLiveApiUrlFormat, holfuy_id, token);
    printf(" - fetching from URL: %s", url);

    CURL *curl_handle;
    CURLcode res;
    int error = 0;
    struct MemoryStruct hf_chunk;
    hf_chunk.memory = malloc(1);  /* will be grown as needed by the realloc above */
    hf_chunk.size = 0;    /* no data at this point */
    curl_handle = curl_easy_init();
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&hf_chunk);
    curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");
    res = curl_easy_perform(curl_handle);
    if(res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));
    } else {
        printf(" : %s\n", hf_chunk.memory);
        char *parsed[20];
        int numToken = 0;
        char *token = strtok(hf_chunk.memory, ",");
        while (token != NULL)
        {
            parsed[numToken++] = token;
            token = strtok(NULL, ",");
        }
        parsed[numToken] = NULL;

        removeSpaces(parsed[0]); // leading space in result string
        if (0 == strcmp(holfuy_id, parsed[0])) {
            // matching station data returned
            sprintf(_weather->id_station, "%s", parsed[0]);
            sprintf(_weather->name, "HF-%s - %s", parsed[0], parsed[1]);
            sprintf(_weather->short_name, "HF-%s", parsed[0]);
            // Time
            _weather->time = 0;
            if ((NULL != parsed[2]) && (NULL != parsed[3])) {
                char time_str[25];
                sprintf(time_str, "%s %s", parsed[2], parsed[3]);
                struct tm tm;
                strftime(time_str, 100, "%Y-%m-%d %H:%M:%S", &tm);
                _weather->time = mktime(&tm);
            }
            // Wind - default: m/s
            _weather->wind = false;
            _weather->wind_speed = 0.0;
            _weather->wind_gusts = 0.0;
            _weather->wind_heading = 0.0;
            if (NULL != parsed[4]) {
                _weather->wind_speed = atof(parsed[4]);
                _weather->wind = true;
            }
            if (NULL != parsed[5]) {
                _weather->wind_gusts = atof(parsed[5]);
                _weather->wind = true;
            }
            if (NULL != parsed[7]) {
                _weather->wind_heading = atof(parsed[7]);
                _weather->wind = true;
            }
            // Temperature - default: C
            _weather->temp = false;
            _weather->temperature = 0.0;
            if (NULL != parsed[8]) {
                _weather->temp = true;
                _weather->temperature = atof(parsed[8]);
            } else {
            }
            // No other sensor so far in 711/795
            _weather->humid = false;
            _weather->humidity = 0.0;
            _weather->barom = false;
            _weather->barometric = 0.0;
        } else {
            error = 1;
        }
    }
    curl_easy_cleanup(curl_handle);
//    free(hf_chunk.memory);
    curl_global_cleanup();
    return error;
}
