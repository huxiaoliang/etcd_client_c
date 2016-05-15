# ETCD C API

the library provide basic etcd C API

 * etcd\_open\_str(server-list [as a string]);

 * etcd\_close\_str

 * etcd\_get(key) returns an etcd\_node structure

 * free\_etcd\_node(etcd\_node \*node)

 * etcd\_watch (prefix, [optional] index) return an etcd\_node structure 

 * etcd\_set(key, value, flag, [optional] prev-condition, [optional] ttl)
    prev-condition can be value/isExist/index
    flag can be EGO_VALUE/EGO_DIR/EGO_ORDER

 * etcd\_set\_dir(key, [optional] ttl);

 * etcd\_delete(key);
 
 * etcd\_self() return an etcd\_self structure containing leader and self status
