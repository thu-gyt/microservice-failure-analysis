#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include <cjson/cJSON.h>

#include "agent_util.h"

char *cgroup_path = NULL;
char *config_path = NULL;

DebugLevel debug_level = NO_DEBUG;

static void print_help(char *prog_name) {
    printf("Usage: %s -c <cgroup_path> -f <config_file_path> [-h] [-i <0|1|2>]\n", prog_name);
    printf("  -c <cgroup_path>\tSpecify the cgroup path that is required for operation.\n");
    printf("  -f <config_file_path>\tSpecify the path to the configuration file.\n");
    printf("  -i <0|1|2>\t\tControl the level of debug information (0 for none, 1 for minimal, 2 for full).\n");
    printf("  -h\t\t\tDisplay this help and exit.\n");
}

void parse_args(int argc, char **argv) {
    int c;

    while ((c = getopt(argc, argv, "c:i:f:h")) != -1) {
        switch (c) {
            case 'h':
                print_help(argv[0]);
                exit(0);
            case 'c':
                cgroup_path = optarg;
                break;
            case 'f':
                config_path = optarg;
                break;
            case 'i':
                if (atoi(optarg) >= 0 && atoi(optarg) <= 2) {
                    debug_level = (DebugLevel)atoi(optarg);
                } else {
                    fprintf(stderr, "Invalid debug level: %s\n", optarg);
                    print_help(argv[0]);
                    exit(1);
                }
                break;
            default:
                print_help(argv[0]);
                exit(1);
        }
    }

    if (cgroup_path == NULL) {
        fprintf(stderr, "Error: The -c option is required.\n");
        print_help(argv[0]);
        exit(1);
    } else {
        INFO("Cgroup path for TCP Option injection set to: %s\n", cgroup_path);
    }

    if (config_path == NULL) {
        fprintf(stderr, "Error: The -f option is required.\n");
        print_help(argv[0]);
        exit(1);
    } else {
        INFO("Config file path set to: %s\n", cgroup_path);
    }
}

int open_client(char *dest_ip, int dst_port) {
    int sockfd, new_fd;
    struct sockaddr_in dest_addr, client_addr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        fprintf(stderr, "Failed to create socket: %s\n", strerror(errno));
        return -1;
    }

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(dst_port);
    if (inet_aton(dest_ip, &dest_addr.sin_addr) == 0) {
        fprintf(stderr, "Invalid address: %s\n", dest_ip);
        close(sockfd);
        return -1;
    }
    memset(&(dest_addr.sin_zero), 0, sizeof(dest_addr.sin_zero));

    if (connect(sockfd, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr)) == -1) {
        fprintf(stderr, "Failed to connect: %s\n", strerror(errno));
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int parse_config_file(const char *filename, ConfigData *config) {
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "Unable to open file: %s\n", filename);
        return -1;
    }

    // Read the entire file
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *data = malloc(length + 1);
    fread(data, 1, length, file);
    fclose(file);
    data[length] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(data);
    if (json == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "Error before: %s\n", error_ptr);
        }
        free(data);
        return -1;
    }

    cJSON *managers = cJSON_GetObjectItemCaseSensitive(json, "managers");
    int num_managers = cJSON_GetArraySize(managers);
    config->num_managers = num_managers;
    config->manager_ips = malloc(num_managers * sizeof(char *));
    config->manager_ports = malloc(num_managers * sizeof(int));
    config->manager_fds = malloc(num_managers * sizeof(int));
    for (int i = 0; i < num_managers; i++) {
        cJSON *manager = cJSON_GetArrayItem(managers, i);
        config->manager_ips[i] = strdup(cJSON_GetObjectItemCaseSensitive(manager, "ip")->valuestring);
        config->manager_ports[i] = cJSON_GetObjectItemCaseSensitive(manager, "port")->valueint;
        DEBUG("Manager %d: IP = %s, Port = %d\n", i, config->manager_ips[i],config->manager_ports[i]);
    }

    cJSON *collectors = cJSON_GetObjectItemCaseSensitive(json, "collectors");
    int num_collectors = cJSON_GetArraySize(collectors);
    config->num_collectors = num_collectors;
    config->collector_ips = malloc(num_collectors * sizeof(char *));
    config->collector_ports = malloc(num_collectors * sizeof(int));
    config->collector_fds = malloc(num_collectors * sizeof(int));
    for (int i = 0; i < num_collectors; i++) {
        cJSON *collector = cJSON_GetArrayItem(collectors, i);
        config->collector_ips[i] = strdup(cJSON_GetObjectItemCaseSensitive(collector, "ip")->valuestring);
        config->collector_ports[i] = cJSON_GetObjectItemCaseSensitive(collector, "port")->valueint;
        DEBUG("Collector %d: IP = %s, Port = %d\n", i, config->collector_ips[i], config->collector_ports[i]);
    }

    // Clean up
    cJSON_Delete(json);
    free(data);

    return 0;
}
