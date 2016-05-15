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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "etcd-api.h"

typedef struct {
  etcd_session sess;
  int key;
  char *value;
} ARGS;

void
*do_set (void *args){
    int i;
    int key;
    char **values;
    char **keys;
    char s[10];

    keys = (char **)malloc(sizeof(char*) * 10000);
    values = (char **)malloc(sizeof(char*) * 10000);

    etcd_session sess;

    key = ((ARGS*)args)->key;
    sess = ((ARGS*)args)->sess;

    for (i = 0; i < 10000; i++){
      sprintf(s, "%d", key + i);
      keys[i] = strdup(s);
      values[i] = ((ARGS*)args)->value;;
    }

    if(etcd_batch_set(sess, keys, values, "test", 10000) != ETCD_OK) {
        fprintf(stderr,"etcd_set failed\n");
        return 0;
    }

    free(args);
    return NULL;
}

int main(int argc, char **argv){
  char            *servers        = "9.111.250.203";
  etcd_session    sess;
  int i, key;
  int error;
  char value[1024];
  long seconds;
  ARGS *args;
  int nthread = 1;


  pthread_t tid[nthread];

  for(i = 0; i < 1024; i++) {
    value[i] = '1';
  }

  sess = etcd_open_str(strdup(servers));
  if (!sess) {
    fprintf(stderr,"etcd_open failed\n");
    return 0;
  }

  seconds = time(NULL);
  printf("current time is %ld\n",seconds);
  
  for(i=0; i< nthread; i++) {

    if (NULL == (args=(ARGS *)malloc(sizeof(ARGS)))) {
      return -1;
    }

    key = i * 1000;
    args->sess = sess;
    args->key = key;
    args->value = value;

    error = pthread_create(&tid[i],
                           NULL,
                           do_set,
                           args);
    if(0 != error)
      fprintf(stderr, "Couldn't run thread number %d, errno %d\n", i, error);
  }

  for(i=0; i< nthread; i++) {
    error = pthread_join(tid[i], NULL);
    fprintf(stderr, "Thread %d terminated\n", i);
  }

  seconds = time(NULL);
  printf("current time is %ld\n",seconds);

  etcd_close_str(sess);
  return 0;
}
