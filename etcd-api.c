/*
 * =====================================================================================
 *
 *       Filename:  etcd-api.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  05/07/2015 10:57:17 AM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  Jian Qiu (qiujian@cn.ibm.com), 
 *   Organization:  IBM
 *
 * =====================================================================================
 */

#if !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <sys/time.h>
#include <unistd.h>
#include "cJSON.h"
#include "etcd-api.h"


#define DEFAULT_ETCD_PORT       2379
#define SL_DELIM                "\n\r\t ,;"

int             g_inited        = 0;


typedef size_t curl_callback_t (void *, size_t, size_t, void *);


#if defined(DEBUG)
void
print_curl_error (char *intro, CURLcode res)
{
            printf("%s: %s\n",intro,curl_easy_strerror(res));
}
#else
#define print_curl_error(intro,res)
#endif


static char *str_replace(char *str, char old, char new){
    char *ptr = str;
    while(*str != '\0'){
        if(*str == old)
            *str = new;
        str++;
    }
    return ptr;
}


static etcd_node_t *get_etcd_node_val(cJSON *obj){
    cJSON *tmpobj = obj;
    cJSON *array;
    int i = 0;
    etcd_node_t *node = (etcd_node_t*) calloc(1, sizeof(etcd_node_t));
    while(tmpobj){
        if(strcmp(tmpobj->string, "value") == 0){
            node->value = strdup(tmpobj->valuestring);
        }
        else if(strcmp(tmpobj->string, "key") == 0){
            node->key = strdup(tmpobj->valuestring);
        }
        else if(strcmp(tmpobj->string, "modifiedIndex") == 0){
            node->modifiedIndex = tmpobj->valueint;
        }
        else if(strcmp(tmpobj->string, "createdIndex") == 0){
            node->createdIndex = tmpobj->valueint;
        }
        if(strcmp(tmpobj->string, "dir") == 0){
            node->dir = tmpobj->valueint;
        }
        else if(strcmp(tmpobj->string, "nodes") == 0){
            node->num_node = cJSON_GetArraySize(tmpobj);
            node->nodes = (void**) calloc(node->num_node, sizeof(etcd_node_t*));
            array = tmpobj->child;
            while(array){
                node->nodes[i++] = get_etcd_node_val(array->child);
                array = array->next;
            }
        }
        tmpobj = tmpobj->next;
    }
    return node;
}


int free_etcd_node(etcd_node_t *node){
    int i;
    free(node->key);
    free(node->value);
    for(i = 0; i < node->num_node; i++){
        free_etcd_node(node->nodes[i]);
    }
    free(node->nodes);
    free(node);
    return 0;
}


int free_etcd_self(etcd_self_t *self){
    free(self->self_id);
    free(self->leader_id);
    free(self->state);
    free(self);
    return 0;
}

static etcd_node_t *get_etcd_node(char *str){
    cJSON *obj = cJSON_Parse(str);
    cJSON *node_obj = cJSON_GetObjectItem(obj, "node");
    etcd_node_t *node = NULL;
    if(node_obj){
        node = get_etcd_node_val(node_obj->child);
    }
    cJSON_Delete(obj);
    return node;
}


static etcd_self_t *get_etcd_self(char *str){
    cJSON *obj = cJSON_Parse(str);
    cJSON *id, *state, *leader;
    etcd_self_t *self = (etcd_self_t*) calloc(1, sizeof(etcd_self_t));
    id = cJSON_GetObjectItem(obj, "id");
    if(id){
        self->self_id = strdup(id->valuestring);
    }
    state = cJSON_GetObjectItem(obj, "state");
    if(state){
        self->state = strdup(state->valuestring);
    }
    leader = cJSON_GetObjectItem(obj, "leaderInfo");
    if(leader){
        leader = cJSON_GetObjectItem(leader, "leader");
        self->leader_id = strdup(leader->valuestring);
    }
    cJSON_Delete(obj);
    return self;
}


etcd_session
etcd_open (etcd_server *server_list){
    _etcd_session   *session;

    if (!g_inited) {
        curl_global_init(CURL_GLOBAL_ALL);
        g_inited = 1;
    }
    session = malloc(sizeof(*session));
    if (!session) {
        return NULL;
    }

    session->servers = server_list;
    return session;
}


void
etcd_close(etcd_session session){
    free(session);
}


static size_t
parse_get_response(void *ptr, size_t size, size_t nmemb, void *stream){
    char           *tmpptr;
    tmpptr = (char*) strdup(ptr);
    tmpptr = str_replace(tmpptr, '\n', '\0');
    *(etcd_node_t**)stream =  get_etcd_node(tmpptr);
    free(tmpptr);
    return size*nmemb;
}


static etcd_result
etcd_get_one(_etcd_session *session, const char *key, etcd_server *srv, const char *prefix,
              const char *post, curl_callback_t cb, void *stream){
    char            *url;
    CURL            *curl;
    CURLcode        curl_res;
    etcd_result     res             = ETCD_ERR;
    void            *err_label      = &&done;
    if (asprintf(&url,"http://%s:%u/v2/%s%s",
                srv->host,srv->port,prefix,key) < 0) {
        goto *err_label;
    }
    err_label = &&free_url;

    curl = curl_easy_init();
    if (!curl) {
        goto *err_label;
    }
    err_label = &&cleanup_curl;

    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_WRITEFUNCTION,cb);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,stream);
    if (post) {
        curl_easy_setopt(curl,CURLOPT_POST,1L);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,post);
    }

    curl_res = curl_easy_perform(curl);
    if (curl_res != CURLE_OK) {
        print_curl_error("perform",curl_res);
        goto *err_label;
    }
    res = ETCD_OK;

cleanup_curl:
    curl_easy_cleanup(curl);
free_url:
    free(url);
done:
    return res;
}


etcd_node_t *etcd_get(etcd_session session_as_void, char *key){
    _etcd_session   *session   = session_as_void;
    etcd_server     *srv;
    etcd_result     res;
    etcd_node_t     *node = NULL;

    for (srv = session->servers; srv->host; ++srv) {
        res = etcd_get_one(session,key,srv, (const char *)"keys/",NULL,
                parse_get_response, (void *)&node);
        if ((res == ETCD_OK) && node) {
            return node;
        }
    }
    return NULL;
}


etcd_node_t *etcd_watch(etcd_session session_as_void, char *pfx, int *index_in){
    _etcd_session   *session   = session_as_void;
    etcd_server     *srv;
    etcd_result     res = ETCD_ERR;
    char            *path = NULL;
    etcd_node_t       *node = NULL;
    if (index_in) {
        if (asprintf(&path,"%s?wait=true&recursive=true&waitIndex=%d",
                     pfx,*index_in) < 0) {
            return NULL;
        }
    }
    else{
        if (asprintf(&path,"%s?wait=true&recursive=true",pfx) < 0) {
            return NULL;
        }
    }
    for (srv = session->servers; srv->host; ++srv) {
        res = etcd_get_one(session,path,srv,"keys/",NULL,
                           parse_get_response, (void*)&node);
        if ((res == ETCD_OK) && node) {
            return node;
        }
    }
    return NULL;
}


static size_t
parse_self_response (void *ptr, size_t size, size_t nmemb, void *stream){
    char           *tmpptr;
    tmpptr = (char*) strdup(ptr);
    tmpptr = str_replace(tmpptr, '\n', '\0');
    *(etcd_self_t**)stream =  get_etcd_self(tmpptr);
    free(tmpptr);
    return size*nmemb;
}


etcd_self_t *etcd_self(etcd_session session_as_void){
    _etcd_session   *session   = session_as_void;
    etcd_server     *srv;
    etcd_result     res;
    etcd_self_t     *self = NULL;

    for (srv = session->servers; srv->host; ++srv) {
        res = etcd_get_one(session,"",srv, (const char *)"stats/self",NULL,
                parse_self_response, (void *)&self);
        if ((res == ETCD_OK) && self) {
            return self;
        }
    }
    return NULL;
}


static size_t
parse_set_response (void *ptr, size_t size, size_t nmemb, void *stream){
    etcd_result     res     = ETCD_PROTOCOL_ERROR;
    res = ETCD_OK;
    *((etcd_result *)stream) = res;
    return size*nmemb;
}


static etcd_result
etcd_set_one (_etcd_session *session, const char *key, const char *value,
              etcd_prevcond_t *precond, unsigned int ttl, etcd_set_flag flag,
              etcd_server *srv){
    char                    *url = NULL;
    char                    *contents       = NULL;
    CURL                    *curl           = NULL;
    etcd_result             res             = ETCD_ERR;
    CURLcode                curl_res;
    void                    *err_label      = &&done;
    char                    *namespace = NULL;
    char                    *http_cmd = NULL;
    char                    *precond_type = NULL;

    namespace = (char *)"v2/keys";
    switch (flag){
        case ETCD_VALUE:
        case ETCD_DIR:
            http_cmd = (char *)"PUT";
            break;
        case ETCD_ORDER:
            http_cmd = (char *)"POST";
            break;
        default:
            http_cmd = (char *)"DELETE";
            break;
    }
    if (asprintf(&url,"http://%s:%u/%s/%s",
                srv->host,srv->port,namespace,key) < 0) {
        goto *err_label;
    }
    err_label = &&free_url;

    if (value) {
        if (asprintf(&contents,"value=%s",value) < 0) {
            goto *err_label;
        }
        err_label = &&free_contents;
    }
    if (flag == ETCD_DIR){
        if (asprintf(&contents,"dir=true") < 0) {
            goto *err_label;
        }
        err_label = &&free_contents;
    }
    if (precond) {
        switch(precond->type){
            case prevValue:
                precond_type = "prevValue";
                break;
            case prevIndex:
                precond_type = "prevIndex";
                break;
            case prevExist:
                precond_type = "prevExist";
                break;
            default:
                precond_type = "prevValue";
                break;
        }
        char *c2;
        if (asprintf(&c2,"%s;%s=%s",contents, precond_type,
                     precond->value) < 0) {
            goto *err_label;
        }
        free(contents);
        contents = c2;
        err_label = &&free_contents;
    }
    if (ttl) {
        char *c2;
        if (asprintf(&c2,"%s;ttl=%u",contents,ttl) < 0) {
            goto *err_label;
        }
        free(contents);
        contents = c2;
        err_label = &&free_contents;
    }

    curl = curl_easy_init();
    if (!curl) {
        goto *err_label;
    }
    err_label = &&cleanup_curl;

    curl_easy_setopt(curl,CURLOPT_CUSTOMREQUEST,http_cmd);
    curl_easy_setopt(curl,CURLOPT_URL,url);
    curl_easy_setopt(curl,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(curl,CURLOPT_POSTREDIR,CURL_REDIR_POST_ALL);

    curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION,
            parse_set_response);
    curl_easy_setopt(curl,CURLOPT_WRITEDATA,&res);

    if (contents) {
        curl_easy_setopt(curl,CURLOPT_POST,1L);
        curl_easy_setopt(curl,CURLOPT_POSTFIELDS,contents);
    }

    curl_res = curl_easy_perform(curl);
    if (curl_res != CURLE_OK) {
        print_curl_error("perform",curl_res);
        goto *err_label;
    }

cleanup_curl:
    curl_easy_cleanup(curl);
free_contents:
    free(contents);
free_url:
    free(url);
done:
    return res;
}


etcd_result
etcd_set (etcd_session session_as_void, char *key, char *value,
          etcd_prevcond_t *precond, etcd_set_flag flag, unsigned int ttl){
    _etcd_session   *session   = session_as_void;
    etcd_server     *srv;
    etcd_result     res = ETCD_ERR;
    for (srv = session->servers; srv->host; ++srv) {
        res = etcd_set_one(session,key,value,precond,ttl,flag,srv);
        if ((res == ETCD_OK) || (res == ETCD_PROTOCOL_ERROR)) {
            return res;
        }
    }

    return ETCD_ERR;
}


static CURL *curl_easy_handler(
    char *url, char *http_cmd, char *contents, etcd_result *res, CURL *_curl) {
  CURL *curl = NULL;

  if (!_curl) {
    curl = curl_easy_init();
  } else {
    curl = _curl;
    curl_easy_reset(curl);
  }
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, http_cmd);
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

  curl_easy_setopt(curl,
                   CURLOPT_WRITEFUNCTION,
                   parse_set_response);
  curl_easy_setopt(curl,CURLOPT_WRITEDATA, res);
  if (contents) {
    curl_easy_setopt(curl,CURLOPT_POST,1L);
    curl_easy_setopt(curl,CURLOPT_POSTFIELDS,contents);
  }

  return curl;
}


static int curl_multi_select(CURLM * curl_m) {
  int ret = 0;

  struct timeval timeout;
  struct timeval wait = { 0, 100 * 1000 };
  fd_set  fd_read;
  fd_set  fd_write;
  fd_set  fd_except;
  int max_fd = -1;
  int ret_code;

  timeout.tv_sec = 1;
  timeout.tv_usec = 0;

  FD_ZERO(&fd_read);
  FD_ZERO(&fd_write);
  FD_ZERO(&fd_except);

  curl_multi_fdset(curl_m, &fd_read, &fd_write, &fd_except, &max_fd);

  if (-1 == max_fd)
  {
    ret_code = select(0, NULL, NULL, NULL, &wait);
  } else {
    ret_code = select(max_fd + 1, &fd_read, &fd_write, &fd_except, &timeout);
  }

  switch(ret_code)
  {
    case -1:
      /* select error */
      ret = -1;
      break;
    case 0:
      /* select timeout */
    default:
      /* one or more of curl's file descriptors say there's data to read or write*/
      ret = 0;
      break;
  }

  return ret;
}


static etcd_result
etcd_set_multi(char **keys, char **values, char *dir, etcd_server *srv, int num) {
  int i;
  int num_curl = 50;
  char *url[num];
  char *contents[num];
  etcd_result res = ETCD_ERR;
  char *namespace = NULL;
  char *http_cmd = NULL;
  int running_handles;

  CURLM *curl_m = curl_multi_init();
  etcd_result responses[num_curl];
  CURL  *curls[num_curl];
  int msgs_left;
  CURLMsg *msg;

  namespace = (char *)"v2/keys";
  http_cmd = (char *)"PUT";

  for (i = 0; i < num_curl; i++) {
    curls[i] = NULL;
  }

  int count = num;
  int start = 0;

  while (count > 0) {
    if (count < num_curl ) {
      num_curl = count;
      count = 0;
    } else {
      num_curl = 50;
      count -= num_curl;
    }

    for (i = 0; i < num_curl; ++i) { 
      asprintf(&url[i + start],
               "http://%s:%u/%s/%s/%s",
                srv->host,srv->port,
                namespace,
                dir,
                keys[i + start]);
      asprintf(&contents[i + start], "value=%s", values[i + start]);
      curls[i] = NULL;
      curls[i] = curl_easy_handler(url[i + start], http_cmd, contents[i + start], &responses[i], curls[i]);
      if (curls[i] == NULL) {
        return -1;
      }
      curl_multi_add_handle(curl_m, curls[i]);
    }

    start += num_curl;

    while (CURLM_CALL_MULTI_PERFORM == curl_multi_perform(curl_m, &running_handles));

    while (running_handles) {
      if (-1 == curl_multi_select(curl_m)) {
        count = 0;
        break;
      }

      while (CURLM_CALL_MULTI_PERFORM == curl_multi_perform(curl_m, &running_handles));
    }

    while((msg = curl_multi_info_read(curl_m, &msgs_left))) {
      if (CURLMSG_DONE == msg->msg) {
        for (i = 0; i < num; ++i) {
          if (msg->easy_handle == curls[i]) {
            break;
          }
        }

        if (i < num && responses[i] != ETCD_OK) {
          res = responses[i];
          fprintf(stderr, "response return error");
          count = 0;
          break; 
        } else if (i < num && responses[i] == ETCD_OK) {
          res = ETCD_OK;
        }
      }
    }

    for (i = 0; i < num_curl; ++i) {
      curl_multi_remove_handle(curl_m, curls[i]);
    }
  }

  for (i = 0; i < num_curl; ++i) {
    curl_easy_cleanup(curls[i]);
  }

  curl_multi_cleanup(curl_m);

  return res;
}

etcd_result
etcd_batch_set(etcd_session session_as_void, char **keys, char **values, char *dir, int num) {
  _etcd_session   *session   = session_as_void;
  etcd_server *srv;
  etcd_result res = ETCD_ERR;
  for (srv = session->servers; srv->host; ++srv) {
    res = etcd_set_multi(keys, values, dir, srv, num);
    if ((res == ETCD_OK) || (res == ETCD_PROTOCOL_ERROR)) {
      return res;
    }
  }

  return res;
}


etcd_result
etcd_set_dir(etcd_session session_as_void, char *key, unsigned int ttl){
    return etcd_set(session_as_void, key, NULL, NULL, ETCD_DIR, ttl);
}


etcd_result
etcd_delete (etcd_session session_as_void, char *key){
    _etcd_session   *session   = session_as_void;
    etcd_server     *srv;
    etcd_result     res        = ETCD_ERR;
    for (srv = session->servers; srv->host; ++srv) {
        res = etcd_set_one(session,key,NULL,NULL,0,0,srv);
        if (res == ETCD_OK) {
            break;
        }
    }
    return res;
}

static void
free_sl (etcd_server *server_list){
    size_t          num_servers;
    for (num_servers = 0; server_list[num_servers].host; ++num_servers) {
        free(server_list[num_servers].host);
    }
    free(server_list);
}


static int
_count_matching (const char *text, const char *cset, int result){
    char    *t;
    int     res     = 0;

    for (t = (char *)text; *t; ++t) {
        if ((strchr(cset,*t) != NULL) != result) {
            break;
        }
        ++res;
    }
    return res;
}


#define count_matching(t,cs)    _count_matching(t,cs,1)
#define count_nonmatching(t,cs) _count_matching(t,cs,0)

etcd_session
etcd_open_str (char *server_names){
    char            *snp;
    int             run_len;
    int             host_len; 
    size_t           num_servers;
    etcd_server     *server_list;
    etcd_session    *session;

    num_servers = 0;
    snp = server_names;
    while (*snp) {
        run_len = count_nonmatching(snp,SL_DELIM);
        if (!run_len) {
            snp += count_matching(snp,SL_DELIM);
            continue;
        }
        ++num_servers;
        snp += run_len;
    }
    if (!num_servers) {
        return NULL;
    }
    server_list = calloc(num_servers+1,sizeof(*server_list));
    if (!server_list) {
        return NULL;
    }
    num_servers = 0;

    snp = server_names;
    while (*snp) {
        run_len = count_nonmatching(snp,SL_DELIM);
        if (!run_len) {
            snp += count_matching(snp,SL_DELIM);
            continue;
        }
        host_len = count_nonmatching(snp,":");
        if ((run_len - host_len) > 1) {
            server_list[num_servers].host = strndup(snp,host_len);
            server_list[num_servers].port = (unsigned short)
                strtoul(snp+host_len+1,NULL,10);
        }
        else{
            server_list[num_servers].host = strndup(snp,run_len);
            server_list[num_servers].port = DEFAULT_ETCD_PORT;
        }
        ++num_servers;
        snp += run_len;
    }
    session = etcd_open(server_list);
    if (!session) {
        free_sl(server_list);
    }
    return session;
}


void
etcd_close_str (etcd_session session){
    free_sl(((_etcd_session *)session)->servers);
    etcd_close(session);
}
