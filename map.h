// Copyright (C) 2024 Aleksei Rogov <alekzzzr@gmail.com>. All rights reserved.

#ifndef _MAP_H
#define _MAP_H

#define MAP_INITIAL_SIZE     15

#define MAP_PARAM_ERROR      -1
#define MAP_NOT_INITIALIZED  -2
#define MAP_MALLOC_ERROR     -3
#define MAP_KEY_ERROR        -4
#define MAP_EMPTY            -5
#define MAP_VALUE_ERROR      -6
#define MAP_OK                0


struct MAP_OBJECT {
    void *key;
    void *value;
    unsigned int key_size;
    unsigned int value_size;
    struct MAP_OBJECT *ptr;
};

struct MAP {
    struct MAP_OBJECT *objects;
    unsigned int length;
    unsigned int count;
    unsigned int iterator_index;
    void *iterator_object;
};

// Add object into 'map'. If map is not initialized, initialize it with default size
// Return error code
int map_add(struct MAP *map, const void *key, unsigned int key_size, const void *value, unsigned int value_size);

// Get object from 'map' by 'key'
// Return value size
int map_get(struct MAP *map, const void *key, unsigned int key_size, void *value, unsigned int value_size);

// Delete key 'key' from the 'map'
// Return error code
int map_del(struct MAP *map, const void *key, unsigned int key_size);

// Prepeare to iterations
// Return error code
int map_get_objects_start(struct MAP *map);

// Get next object from the map
// Return found object or NULL
struct MAP_OBJECT *map_get_objects_next(struct MAP *map);

// Init 'map' with initial length of 'length'
// Return error code
int map_init(struct MAP *map, unsigned int length);

// Destroy 'map'
// Return error code
int map_destroy(struct MAP *map);

#endif