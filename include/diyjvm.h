#ifndef DIYJVM_H
#define DIYJVM_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// Debug flag
extern bool debug_mode;

#define DEBUG_PRINT(fmt, ...)                                \
    do {                                                     \
        if (debug_mode) {                                    \
            fprintf(stderr, "[DEBUG] " fmt, ##__VA_ARGS__);  \
        }                                                    \
    } while (0)

// Safe free macro
#define SAFE_FREE(p)            \
    do {                        \
        if ((p) != NULL) {      \
            free(p);            \
            (p) = NULL;         \
        }                       \
    } while (0)

// Error handling macro
#define ERROR_AND_CLEANUP(msg, cleanup_block) \
    do {                                      \
        fprintf(stderr, "Error: %s\n", msg);  \
        cleanup_block;                        \
        return NULL;                          \
    } while(0)

#define JAVA_MAGIC 0xCAFEBABE
#define MAX_STRING_LENGTH 65535
#define MAX_CONSTANT_POOL_SIZE 32767

// Constant pool tags
#define CONSTANT_Class               7
#define CONSTANT_Fieldref            9
#define CONSTANT_Methodref           10
#define CONSTANT_InterfaceMethodref  11
#define CONSTANT_String              8
#define CONSTANT_Integer             3
#define CONSTANT_Float               4
#define CONSTANT_Long                5
#define CONSTANT_Double              6
#define CONSTANT_NameAndType         12
#define CONSTANT_Utf8                1

typedef struct {
    uint16_t max_stack;
    uint16_t max_locals;
    uint32_t code_length;
    uint8_t *code;
    // For brevity, we skip the rest (exception table, inner attributes, etc.)
} code_attribute;

typedef struct {
    uint8_t tag;
    union {
        struct {
            uint16_t name_index;
        } class_info;
        struct {
            uint32_t bytes;
        } integer_info;
        struct {
            uint16_t string_index;
        } string_info;
        struct {
            uint16_t class_index;
            uint16_t name_and_type_index;
        } methodref_info;
        struct {
            uint16_t name_index;
            uint16_t descriptor_index;
        } nameandtype_info;
        struct {
            uint16_t length;
            char *bytes;
        } utf8_info;
        struct {
            // For Long/Double, each is 8 bytes total
            uint32_t high_bytes;
            uint32_t low_bytes;
        } long_info;
        // ... other possible constant pool entries
    } info;
} cp_info;

typedef struct {
    uint16_t access_flags;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t attributes_count;
    code_attribute *code_attribute; // We store only the "Code" attribute for demo
} method_info;

typedef struct {
    uint32_t magic;
    uint16_t minor_version;
    uint16_t major_version;
    uint16_t constant_pool_count;
    cp_info *constant_pool;

    uint16_t access_flags;
    uint16_t this_class;
    uint16_t super_class;
    uint16_t interfaces_count;

    uint16_t fields_count;  // We'll skip storing the fields themselves
    uint16_t methods_count;
    method_info *methods;
} ClassFile;


ClassFile *read_class_file(const char *filename);

void free_class_file(ClassFile *cf);

#endif //DIYJVM_H
