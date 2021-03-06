/*
 * app_socket
 *
 * Copyright (C) 2015
 * Miho <miho@miho.org.ua>
 * http://miho.org.ua
 *
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision$")

#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/cli.h"
#include "asterisk/pbx.h"

#define MODULE_DESCRIPTION  "Miho Socket app"

// Register a name of application in dialplan
static char *app_socket = "Socket";

typedef struct _json_keys {
    char *key;
    char *data;
} json_keys;


static char *m_strcpy(const char *src, const int count) {
    char *tmp;
    if (count == 0) {
        tmp = (char *) calloc(strlen(src), sizeof(char*));
        strcpy(tmp, src);
    } else {
        tmp = (char *) calloc(count, sizeof(char*));
        strncpy(tmp, src, count);
    }
    return tmp;
}

static char *m_strcat(char *dst, const char *src, const int count) {
    char *tmp;
    if (count == 0) {
        tmp = (char *) calloc(strlen(dst) + strlen(src), sizeof(char*));
        strcpy(tmp, dst);
        strcat(tmp, src);
    } else {
        tmp = (char *) calloc(strlen(dst) + count, sizeof(char*));
        strcpy(tmp, dst);
        strncat(tmp, src, count);
    }
    return tmp;
}

static int getJson(json_keys *keys, const char *json, const int start_pos, const char *parent) {
    char *new_json = (char*) calloc(0, sizeof(char*));
    char *new_parent = (char*) calloc(0, sizeof(char*));
    int found_key = 1;
    int found_data = 0;
    int sub_json = 0;
    int i;
    int keys_count = start_pos;
    int data_count = start_pos;

    if (json[0] != '{' || json[strlen(json + 1)] != '}')
        return 0;

    for (i = 0; i < strlen(json); i++)
        if (json[i] != ' ' && json[i] != '"' && json[i] != '\'')
            new_json = m_strcat(new_json, &json[i], 1);

    keys[keys_count].key = m_strcpy(parent, 0);

    for (i = 1; i < strlen(new_json) - 1; i++) {
        /* Начался json в json */
        if (new_json[i] == '{')
            sub_json += 1;

        if (sub_json == 0) {

            /* Если ключ считывается то заносим данные к ключу, : означает конец ключа */
            if (found_key == 1 && new_json[i] != ':') {
                keys[keys_count].key = m_strcat(keys[keys_count].key, &new_json[i], 1);
            }

            /* Запятая означает начало считывания ключа */
            if (new_json[i] == ',') {
                keys[keys_count].key = m_strcpy(parent, 0);
                found_key = 1;
            }

            /* Если ключ считывается ищем :, она означает конец ключа */
            if (new_json[i] == ':' && found_key == 1) {
                keys_count++;
                found_key = 0;
            }

            /* Парсим значения ключей */

            /* Если данные считываются то заносим данные к данным, , означает конец данных */
            if (found_data == 1 && new_json[i] != ',') {
                keys[data_count].data = m_strcat(keys[data_count].data, &new_json[i], 1);
            }

            /* : означает начало считывания ключа */
            if (new_json[i] == ':') {
                keys[data_count].data = (char*) calloc(0, sizeof(char*));
                found_data = 1;
            }

            /* Если ключ считывается ищем , , она означает конец ключа */
            if (new_json[i] == ',' && found_data == 1) {
                data_count++;
                found_data = 0;
            }
        } else {
            keys[data_count].data = m_strcat(keys[data_count].data, &new_json[i], 1);
        }

        /* Закончился json в json */
        if (new_json[i] == '}' && sub_json > 0) {
            sub_json -= 1;
            if (sub_json == 0) {
                new_parent = m_strcat(new_parent, keys[keys_count - 1].key, 0);
                new_parent = m_strcat(new_parent, "_", 0);
                keys_count = getJson(keys, keys[data_count].data, keys_count, new_parent);
                data_count = keys_count - 1;
            }
        }
    }

    return keys_count;
}

// Send data to server and receive answer
static char *send_socket(const char *host, const int port, const char *message) {
    struct timeval tv;
    int buf_len = 4096;
    char buf[buf_len], tmp[buf_len];
    int sock, len;
    struct sockaddr_in addr;
    struct hostent *hp;
    struct ast_hostent he;

    tv.tv_sec = 5; // Connect timeout in sec
    tv.tv_usec = 5;

    // Creating socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        ast_log(LOG_ERROR, "Cannot create socket!\n");
        return 0;
    }

    // Set options of connect
    // Set timeout
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO,&tv,sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv));
    // Set interface and port    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    // Getting IP from Hostname
    hp = ast_gethostbyname(host, &he);
    if (hp) {
        memcpy(&addr.sin_addr, hp->h_addr, sizeof(addr.sin_addr));
    } else {
        ast_log(LOG_ERROR, "Could not resolve hostname!\n");
        close(sock);
        return 0;
    }

    // Connecting to server
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)     
    {
        ast_log(LOG_ERROR, "Cannot connect to server!\n");
        close(sock);
        return 0;
    }

    // Send message to server. Len "+1" is needed!!!
    send(sock, message, strlen(message) + 1, 0);

    // Receive data
    if ((len = recv(sock, buf, buf_len, 0)) <= 0) {
        ast_log(LOG_ERROR, "Dont receive response from host!\n");
        close(sock);
        return 0;
    }

    // Fixing a bug, when recv func receive text
    // with previous data (of this data short)
    //char *tmp = (char *) calloc(len, sizeof(char *));
    ast_copy_string(tmp, buf, len + 1);

    close(sock);

    return ast_strdupa(tmp);
}

static int *set_vars_from_json(struct ast_channel *chan, const char *text) {
    json_keys *keys = (json_keys*) calloc(100, sizeof(json_keys*));
    char *tmp =(char*) calloc(0, sizeof(char*));
    int json_args = 0;
    int i;
    int c;

    c = getJson(keys, text, json_args, "");

    if (c > 0) {
        for (i = 0; i < c; i++) {
            ast_log(LOG_NOTICE, "JSON: %s-%s\n", keys[i].key, keys[i].data);
            tmp = (char*) realloc(tmp, (strlen("SOCKET_") + strlen(keys[i].key)) * sizeof(char*) );
            strcpy(tmp, "SOCKET_");
            strcat(tmp, keys[i].key);
            pbx_builtin_setvar_helper(chan, tmp, S_OR(keys[i].data, NULL));
            
        }
    } else {
        pbx_builtin_setvar_helper(chan, "SOCKET_DATA", S_OR(text, NULL));
    }

    

    /*if ((s = ast_json_load_string(text, NULL)) == NULL) {
        ast_log(LOG_WARNING, "NO JSON\n");
        return 0;
    }
    json_args = ast_json_object_size(s);
    //ast_log(LOG_ERROR, "JSON: %ju\n", ast_json_integer_get(ast_json_object_get(s, "test")));
    for (i = 1; i <= json_args; i++) {
        if (ast_json_string_get(ast_json_object_get(s, "test")) != NULL ) {
            ast_log(LOG_ERROR, "JSON: %s\n", ast_json_string_get(ast_json_object_get(s, "")));
        } else if (ast_json_integer_get(ast_json_object_get(s, "test")) != 0 ) {
            ast_log(LOG_ERROR, "JSON: %ju\n", ast_json_integer_get(ast_json_object_get(s, "test")));
        }
    }*/

    

    return 0;

    // Array size ast_json_object_size(s))
    // From json to string ast_json_dump_string('s')
    // From string to json ast_json_load_string('s', NULL);
    // Get from json ast_json_string_get(ast_json_object_get(s, 'key'))
}

// Func used when calling Socket app in dialplan
static int socket_exec(struct ast_channel *chan, const char *data) {
    char *host, *message, *tmp;
    int port;

    AST_DECLARE_APP_ARGS(args,
                AST_APP_ARG(host);
                AST_APP_ARG(port);
                AST_APP_ARG(message);
        );

    // Arguments is needed
    if (ast_strlen_zero(data)) {
        ast_log(LOG_ERROR, "Socket app need arguments!\n");
        return 0;
    }

    tmp = ast_strdupa(data);
    AST_STANDARD_APP_ARGS(args, tmp);

    if (ast_strlen_zero(args.host)) {
        ast_log(LOG_ERROR, "Socket app need host, port and message text!\n");
        return 0;
    } else if (ast_strlen_zero(args.port)) {
        ast_log(LOG_ERROR, "Socket app also need port and message text!\n");
        return 0;
    } else if (ast_strlen_zero(args.message)) {
        ast_log(LOG_ERROR, "Socket app also need message text!\n");
        return 0;
    }

    host = ast_strdupa(args.host);
    port = atoi(ast_strdupa(args.port));
    message = ast_strdupa(args.message);

    char *socket_data = send_socket(host, port, message); // Send data

    // Set asterisk variables id dialplan
    set_vars_from_json(chan, socket_data);

    return 0;
}

// Func calling from CLI "socket test text"
static char *handle_cli_socket_test(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a) {
    char *host, *message;
    char *ret = CLI_FAILURE;
    int port;

    // Register CLI command
    switch (cmd) {
        case CLI_INIT:
        e->command = "socket test";
        e->usage =
        "Usage: socket test <text>\n";
        return NULL;
        case CLI_GENERATE:
        return NULL;
    }

    ast_module_ref(ast_module_info->self);

    // Arguments is needed
    if (ast_strlen_zero(a->argv[2])) {
        ast_log(LOG_ERROR, "Socket app need host, port and message text!\n");
        return 0;
    } else if (ast_strlen_zero(a->argv[3])) {
        ast_log(LOG_ERROR, "Socket app also need port and message text!\n");
        return 0;
    } else if (ast_strlen_zero(a->argv[4])) {
        ast_log(LOG_ERROR, "Socket app also need message text!\n");
        return 0;
    }

    host = ast_strdupa(a->argv[2]);
    port = atoi(ast_strdupa(a->argv[3]));
    message = ast_strdupa(a->argv[4]);

    char *res = send_socket(host, port, message);
    if (res) {
        ast_verb(0, "Received: %s\n", res);
    }

    ret = CLI_SUCCESS;
    ast_module_unref(ast_module_info->self);
    return ret;
}

// Register CLI applications and it func
static struct ast_cli_entry cli_socket[] = {
    AST_CLI_DEFINE(handle_cli_socket_test, "Test socket")
};

/*!
 * \brief Reload the module
 */
static int reload_module(void)
{
        return 0;
}

// Load module. CLI: module load app_socket.so
static int load_module(void) {
    int res;

    res = ast_register_application_xml(app_socket, socket_exec);

    ast_cli_register_multiple(cli_socket, ARRAY_LEN(cli_socket));


    return res;
}

// Unload module. CLI: module unload app_socket.so
static int unload_module(void) {
    int res;

    res = ast_unregister_application(app_socket);

    ast_cli_unregister_multiple(cli_socket, ARRAY_LEN(cli_socket));

    return res;
}

// Info about this module for asterisk
//AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Miho Socket app");
AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, MODULE_DESCRIPTION,
        .load = load_module,
        .unload = unload_module,
        .reload = reload_module,
);