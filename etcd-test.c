/*
 * =====================================================================================
 *
 *       Filename:  etcd-test.c
 *
 *    Description:  
 *
 *        Version:  1.0
 *        Created:  05/07/2015 04:29:45 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  YOUR NAME (), 
 *   Organization:  
 *
 * =====================================================================================
 */

#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "etcd-api.h"


void print_etcd_node(etcd_node_t *node){
    int i;
    if(node->key){
        printf("key: %s ", node->key);
    }
    if(node->value){
        printf("value: %s", node->value);
    }
    printf("\n");
    if(node->dir){
        for(i = 0; i < node->num_node; i++){
            print_etcd_node((etcd_node_t*)node->nodes[i]);
        }
    }
}


void print_etcd_self(etcd_self_t *self){
    printf("leader: %s \n", self->leader_id);
    printf("self: %s \n", self->self_id);
    printf("state: %s \n", self->state);
}

int
do_get (etcd_session sess, char *key){
    etcd_node_t *node = NULL;
    printf("getting %s\n",key);
    node = etcd_get(sess,key);
    if(node){
        print_etcd_node(node);
        free_etcd_node(node);
    }
    return 0;
}


int
do_watch (etcd_session sess, char *pfx, char *index_str){
    etcd_node_t       *node;
    int             index_i;
    if (index_str) {
        index_i = (int)strtol(index_str,NULL,10);
    }
    for (;;) {
        node = etcd_watch(sess,pfx,&index_i);
        if(node){
            print_etcd_node(node);
            free_etcd_node(node);
        }
        else{
            printf("I don't know what happened\n");
        }
        sleep(1);

    }
    return 0;
}


int
do_set (etcd_session sess, char *key, char *value, etcd_prevcond_t *precond, 
        etcd_set_flag flag, char *ttl){
    unsigned long           ttl_num = 0;
    printf("setting %s to %s\n",key,value);
    if (precond) {
        printf("  precond = %d %s\n",precond->type, precond->value);
    }
    if (ttl) {
        ttl_num = strtoul(ttl,NULL,10);
        printf("  ttl = %lu\n",ttl_num);
    }
    else {
        ttl_num = 0;
    }
    if (etcd_set(sess,key,value,precond,flag,ttl_num) != ETCD_OK) {
        fprintf(stderr,"etcd_set failed\n");
        return 0;
    }
    return 0;

}


int do_set_dir(etcd_session sess, char *key, char *ttl){
    unsigned long           ttl_num = 0;
    printf("setting directory %s\n",key);
    if (ttl) {
        ttl_num = strtoul(ttl,NULL,10);
        printf("  ttl = %lu\n",ttl_num);
    }
    else {
        ttl_num = 0;
    }
    if (etcd_set_dir(sess,key,ttl_num) != ETCD_OK) {
        fprintf(stderr,"etcd_setdir failed\n");
        return 0;
    }
    return 0;
}


int
do_delete (etcd_session sess, char *key){
    printf("deleting %s\n",key);
    if (etcd_delete(sess,key) != ETCD_OK) {
        fprintf(stderr,"etcd_delete failed\n");
        return !0;
    }
    return 0;
}



int
do_leader (etcd_session sess){
    etcd_self_t *self = etcd_self(sess);
    if (!self) {
        fprintf(stderr,"etcd_delete failed\n");
        return !0;
    }
    print_etcd_self(self);
    free_etcd_self(self);
    return 0;
}


struct option my_opts[] = {
    { "index",      required_argument,      NULL,   'w' },
    { "precond",    required_argument,      NULL,   'p' },
    { "servers",    required_argument,      NULL,   's' },
    { "ttl",        required_argument,      NULL,   't' },
    { NULL }
};


int
print_usage (char *prog){
    fprintf (stderr, "Usage: %s [-s server-list] command ...\n",prog);
    fprintf (stderr, "Valid commands:\n");
    fprintf (stderr, "  get       KEY\n");
    fprintf (stderr, "  set       [-p precond] [-t ttl] KEY VALUE\n");
    fprintf (stderr, "  delete    KEY\n");
    fprintf (stderr, "  watch     [-i index] KEY\n");
    fprintf (stderr, "  leader\n");
    fprintf (stderr, "  lock     -t ttl [-i index] KEY\n");
    fprintf (stderr, "  unlock    -i index KEY\n");
    fprintf (stderr, "Server list is host:port pairs separated by comma,\n"
                     "semicolon, or white space.  If not given on the\n"
                     "command line, ETCD_SERVERS will be used from the\n"
                     "environment instead.\n");

    return !0;
}


int main(int argc, char **argv){
    int             opt;
    char            *servers        = getenv("ETCD_SERVERS");
    char            *command;
    etcd_prevcond_t *precond        = NULL; 
    char            *ttl            = NULL;
    char            *index_str      = NULL;
    int             parsed          = 0;
    etcd_session    sess;
    int             res             = !0;
    int             flag            = 0;
    for (;;) {
        opt = getopt_long(argc,argv,"i:p:v:s:t:f:",my_opts,NULL);
        if (opt == (-1)) {
            break;
        }
        switch (opt) {
            case 'i':
                index_str = optarg;
                break;
            case 'p':
                if(!precond){
                    precond = malloc(sizeof(etcd_prevcond_t));
                }
                precond->type = atoi(optarg);
                break;
            case 'v':
                if(!precond){
                    precond = malloc(sizeof(etcd_prevcond_t));
                }
                precond->value = optarg;
                break;
            case 's':
                servers = optarg;
                break;
            case 't':
                ttl = optarg;
                break;
            case 'f':
                flag = atoi(optarg);
                break;
            default:
                return print_usage(argv[0]);
        }
    }
    if (!servers) {
        return print_usage(argv[0]);
    }
    if (optind == argc) {
        return print_usage(argv[0]);
    }

    sess = etcd_open_str(strdup(servers));
    if (!sess) {
        fprintf(stderr,"etcd_open failed\n");
        return 0;
    }

    command = argv[optind++];
    if (!strcasecmp(command,"get")) {
        if (((argc-optind) == 1) && !precond && !ttl && !index_str) {
            parsed = 1;
            do_get(sess,argv[optind]);
            res = 0;
        }
    }
    else if (!strcasecmp(command,"set")) {
        if (((argc-optind) == 2) && !index_str) {
            parsed = 1;
            res = do_set (sess, argv[optind], argv[optind+1],
                          precond, flag, ttl);
            if(precond){
                free(precond);
            }
        }
    }
    else if (!strcasecmp(command,"setdir")) {
        if (((argc-optind) == 1) && !index_str) {
            parsed = 1;
            res = do_set_dir(sess, argv[optind], ttl);
        }
    }
    else if (!strcasecmp(command,"delete")) {
        if (((argc-optind) == 1) && !precond && !ttl && !index_str) {
            parsed = 1;
            res = do_delete(sess,argv[optind]);
        }
    }
    else if (!strcasecmp(command,"watch")) {
        if (((argc-optind) == 1) && !precond && !ttl) {
            parsed = 1;
            res = do_watch(sess,argv[optind],index_str);
        }
    }
    else if (!strcasecmp(command,"leader")) {
        parsed = 1;
        do_leader(sess);
    }
    etcd_close_str(sess);
    return parsed ? res : print_usage(argv[0]);
}
