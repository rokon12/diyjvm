#include "../include/diyjvm.h"
#include <string.h>

bool debug_mode = false;

// Class file parsing utilities

static int safe_fread(void *buffer, size_t size, size_t count, FILE *fp) {
    const size_t n = fread(buffer, size, count, fp);
    if (n < count) {
        // Could be EOF or an error
        if (ferror(fp)) {
            fprintf(stderr, "Error: fread() encountered an I/O error.\n");
        } else {
            fprintf(stderr, "Error: Unexpected end of file.\n");
        }
        return 0;
    }
    return 1;
}

static uint32_t read_u4(FILE *fp, bool *ok) {
    uint32_t value = 0;
    if (!safe_fread(&value, 4, 1, fp)) {
        *ok = false;
        return 0;
    }
    return __builtin_bswap32(value); // Convert from big-endian
}

static uint16_t read_u2(FILE *fp, bool *ok) {
    uint16_t value = 0;
    if (!safe_fread(&value, 2, 1, fp)) {
        *ok = false;
        return 0;
    }
    // Convert from big-endian
    return __builtin_bswap16(value);
}

static uint8_t read_u1(FILE *fp, bool *ok) {
    uint8_t value = 0;
    if (!safe_fread(&value, 1, 1, fp)) {
        *ok = false;
        return 0;
    }
    return value;
}

static int read_constant_pool_entry(FILE *fp, cp_info *entry, bool *ok) {
    entry->tag = read_u1(fp, ok);
    if (!*ok) return 0;

    DEBUG_PRINT("Reading constant pool entry with tag: %d\n", entry->tag);

    switch (entry->tag) {
        case CONSTANT_Class:
            entry->info.class_info.name_index = read_u2(fp, ok);
            break;

        case CONSTANT_Utf8: {
            uint16_t length = read_u2(fp, ok);
            if (!*ok) return 0;

            if (length > MAX_STRING_LENGTH) {
                fprintf(stderr, "Error: UTF8 string too long (%u)\n", length);
                *ok = false;
                return 0;
            }
            entry->info.utf8_info.length = length;
            entry->info.utf8_info.bytes = (char *) malloc(length + 1);
            if (!entry->info.utf8_info.bytes) {
                fprintf(stderr, "Error: Out of memory for UTF8 string.\n");
                *ok = false;
                return 0;
            }
            if (!safe_fread(entry->info.utf8_info.bytes, 1, length, fp)) {
                *ok = false;
                return 0;
            }
            entry->info.utf8_info.bytes[length] = '\0';
            break;
        }

        case CONSTANT_Integer:
            entry->info.integer_info.bytes = read_u4(fp, ok);
            break;

        case CONSTANT_String:
            entry->info.string_info.string_index = read_u2(fp, ok);
            break;

        case CONSTANT_Fieldref:
        case CONSTANT_Methodref:
        case CONSTANT_InterfaceMethodref:
            entry->info.methodref_info.class_index = read_u2(fp, ok);
            entry->info.methodref_info.name_and_type_index = read_u2(fp, ok);
            break;

        case CONSTANT_NameAndType:
            entry->info.nameandtype_info.name_index = read_u2(fp, ok);
            entry->info.nameandtype_info.descriptor_index = read_u2(fp, ok);
            break;

        case CONSTANT_Long:
        case CONSTANT_Double:
            // Each consumes 8 bytes
            entry->info.long_info.high_bytes = read_u4(fp, ok);
            entry->info.long_info.low_bytes = read_u4(fp, ok);
        // According to JVM spec, Long/Double uses two entries in the CP.
        // Return "2" so the loop can skip the next slot.
            return 2;

        default:
            // For unknown tags, skip gracefully if possible
            DEBUG_PRINT("Unknown constant pool entry tag: %d. Skipping.\n", entry->tag);
        // We do not know how many bytes to skip. Minimally do nothing
        // or handle it if you have a custom extension.
            break;
    }

    if (!*ok) return 0;

    return 1; // Normal case
}

ClassFile *read_class_file(const char *filename) {
    DEBUG_PRINT("Opening class file: %s\n", filename);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg), "Failed to open class file '%s'.", filename);
        ERROR_AND_CLEANUP(error_msg, { /* no cleanup needed here */ });
    }

    bool ok = true;
    ClassFile *cf = malloc(sizeof(ClassFile));
    if (!cf) {
        ERROR_AND_CLEANUP("Out of memory allocating ClassFile.", {
            fclose(file);
        });
    }
    memset(cf, 0, sizeof(*cf)); // zero out structure

    // Read magic
    cf->magic = read_u4(file, &ok);
    DEBUG_PRINT("Read magic number: 0x%08X\n", cf->magic);
    if (!ok || cf->magic != JAVA_MAGIC) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "Invalid or missing magic number in '%s'.", filename);
        ERROR_AND_CLEANUP(error_msg, {
            free_class_file(cf);
            fclose(file);
        });
    }
    DEBUG_PRINT("Magic number verified successfully\n");

    // Read minor/major version
    cf->minor_version = read_u2(file, &ok);
    cf->major_version = read_u2(file, &ok);
    if (!ok) {
        ERROR_AND_CLEANUP("Could not read version numbers.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    if (cf->major_version < 45 || cf->major_version > 69) {
        ERROR_AND_CLEANUP("Unsupported class file version.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    // Read constant pool count
    cf->constant_pool_count = read_u2(file, &ok);
    DEBUG_PRINT("Constant pool count: %d\n", cf->constant_pool_count);
    if (!ok || cf->constant_pool_count > MAX_CONSTANT_POOL_SIZE) {
        ERROR_AND_CLEANUP("Invalid constant pool count.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    cf->constant_pool = (cp_info *) calloc(cf->constant_pool_count, sizeof(cp_info));
    if (!cf->constant_pool) {
        ERROR_AND_CLEANUP("Out of memory allocating constant pool.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    // Read each CP entry
    for (int i = 1; i < cf->constant_pool_count;) {
        int step = read_constant_pool_entry(file, &cf->constant_pool[i], &ok);
        if (!ok || step == 0) {
            char error_msg[256];
            snprintf(error_msg, sizeof(error_msg),
                     "Failed reading constant pool entry at index %d.", i);
            ERROR_AND_CLEANUP(error_msg, {
                free_class_file(cf);
                fclose(file);
            });
        }
        i += step; // account for LONG/DOUBLE
    }

    // Read access_flags, this_class, super_class
    cf->access_flags = read_u2(file, &ok);
    cf->this_class   = read_u2(file, &ok);
    cf->super_class  = read_u2(file, &ok);
    if (!ok) {
        ERROR_AND_CLEANUP("Could not read class header (flags/this/super).", {
            free_class_file(cf);
            fclose(file);
        });
    }

    // Interfaces
    cf->interfaces_count = read_u2(file, &ok);
    if (!ok) {
        ERROR_AND_CLEANUP("Could not read interfaces_count.", {
            free_class_file(cf);
            fclose(file);
        });
    }
    if (cf->interfaces_count > 0) {
        long skipBytes = cf->interfaces_count * 2L;
        if (fseek(file, skipBytes, SEEK_CUR) != 0) {
            ERROR_AND_CLEANUP("Seek failed when skipping interfaces.", {
                free_class_file(cf);
                fclose(file);
            });
        }
    }

    // Fields
    cf->fields_count = read_u2(file, &ok);
    if (!ok) {
        ERROR_AND_CLEANUP("Could not read fields_count.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    // Skip over field details entirely (minimal example)
    for (int i = 0; i < cf->fields_count; i++) {
        uint16_t field_access     = read_u2(file, &ok);
        uint16_t field_name       = read_u2(file, &ok);
        uint16_t field_desc       = read_u2(file, &ok);
        uint16_t field_attr_count = read_u2(file, &ok);

        DEBUG_PRINT("Field %d: access_flags=0x%04X, name_index=%d, descriptor_index=%d, attributes_count=%d\n",
                    i, field_access, field_name, field_desc, field_attr_count);

        if (!ok) {
            ERROR_AND_CLEANUP("Could not read field info.", {
                free_class_file(cf);
                fclose(file);
            });
        }

        // Skip all attributes of this field
        for (int j = 0; j < field_attr_count; ++j) {
            uint16_t attr_name_index = read_u2(file, &ok);
            uint32_t attr_length     = read_u4(file, &ok);
            DEBUG_PRINT("Field %d, Attribute %d: name_index=%d, length=%d\n",
                        i, j, attr_name_index, attr_length);
            if (!ok) {
                ERROR_AND_CLEANUP("Error reading field attribute name/length.", {
                    free_class_file(cf);
                    fclose(file);
                });
            }
            if (fseek(file, attr_length, SEEK_CUR) != 0) {
                ERROR_AND_CLEANUP("Seek failed skipping field attribute.", {
                    free_class_file(cf);
                    fclose(file);
                });
            }
        }
    }

    // Methods
    cf->methods_count = read_u2(file, &ok);
    DEBUG_PRINT("Methods count: %d\n", cf->methods_count);
    if (!ok) {
        ERROR_AND_CLEANUP("Could not read methods_count.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    // Arbitrary sanity check
    if (cf->methods_count > 1000) {
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                 "Method count %u is suspiciously large.", cf->methods_count);
        ERROR_AND_CLEANUP(error_msg, {
            free_class_file(cf);
            fclose(file);
        });
    }

    cf->methods = (method_info *) calloc(cf->methods_count, sizeof(method_info));
    if (!cf->methods) {
        ERROR_AND_CLEANUP("Out of memory allocating methods.", {
            free_class_file(cf);
            fclose(file);
        });
    }

    for (int i = 0; i < cf->methods_count; i++) {
        method_info *method = &cf->methods[i];
        method->access_flags     = read_u2(file, &ok);
        method->name_index       = read_u2(file, &ok);
        method->descriptor_index = read_u2(file, &ok);
        method->attributes_count = read_u2(file, &ok);

        DEBUG_PRINT("Method[%d]: access=0x%04X, name_index=%d, desc_index=%d, attr_count=%d\n",
                    i, method->access_flags, method->name_index,
                    method->descriptor_index, method->attributes_count);

        if (!ok) {
            ERROR_AND_CLEANUP("Could not read method info.", {
                free_class_file(cf);
                fclose(file);
            });
        }

        // Check each method attribute
        for (int j = 0; j < method->attributes_count; j++) {
            uint16_t attribute_name_index = read_u2(file, &ok);
            uint32_t attr_length = read_u4(file, &ok);
            if (!ok) {
                ERROR_AND_CLEANUP("Error reading attribute name index/length for method attribute.", {
                    free_class_file(cf);
                    fclose(file);
                });
            }

            // If it's "Code" attribute
            if (attribute_name_index < cf->constant_pool_count) {
                cp_info *attrName = &cf->constant_pool[attribute_name_index];
                if (attrName->tag == CONSTANT_Utf8 &&
                    strcmp(attrName->info.utf8_info.bytes, "Code") == 0) {

                    DEBUG_PRINT(" -> Found Code attribute\n");
                    method->code_attribute = (code_attribute *) calloc(1, sizeof(code_attribute));
                    if (!method->code_attribute) {
                        ERROR_AND_CLEANUP("Out of memory for code_attribute.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }

                    code_attribute *code = method->code_attribute;
                    code->max_stack  = read_u2(file, &ok);
                    code->max_locals = read_u2(file, &ok);
                    code->code_length = read_u4(file, &ok);

                    if (!ok) {
                        ERROR_AND_CLEANUP("Could not read code_attribute core fields.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }

                    code->code = (uint8_t *) malloc(code->code_length);
                    if (!code->code) {
                        ERROR_AND_CLEANUP("Out of memory for method code.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }
                    if (!safe_fread(code->code, 1, code->code_length, file)) {
                        ERROR_AND_CLEANUP("Could not read code bytes.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }

                    uint16_t exception_table_length = read_u2(file, &ok);
                    if (!ok) {
                        ERROR_AND_CLEANUP("Could not read exception_table_length.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }
                    long skipBytes = exception_table_length * 8L;
                    if (fseek(file, skipBytes, SEEK_CUR) != 0) {
                        ERROR_AND_CLEANUP("Seek failed skipping exception table.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }

                    uint16_t code_attr_count = read_u2(file, &ok);
                    if (!ok) {
                        ERROR_AND_CLEANUP("Could not read code attribute_count.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }

                    // Skip sub-attributes of Code
                    for (int k = 0; k < code_attr_count; k++) {
                        uint16_t sub_attr_name_idx = read_u2(file, &ok);
                        uint32_t sub_attr_len      = read_u4(file, &ok);
                        DEBUG_PRINT("Method[%d], Code attribute, Sub-attribute %d: name_index=%d, length=%d\n",
                                    i, k, sub_attr_name_idx, sub_attr_len);
                        if (!ok) {
                            ERROR_AND_CLEANUP("Error reading code sub-attribute name/length in Code attribute.", {
                                free_class_file(cf);
                                fclose(file);
                            });
                        }
                        if (fseek(file, sub_attr_len, SEEK_CUR) != 0) {
                            ERROR_AND_CLEANUP("Seek failed skipping sub-attribute in Code.", {
                                free_class_file(cf);
                                fclose(file);
                            });
                        }
                    }
                } else {
                    // Skip unknown method attribute
                    if (fseek(file, attr_length, SEEK_CUR) != 0) {
                        ERROR_AND_CLEANUP("Seek failed skipping unknown method attribute.", {
                            free_class_file(cf);
                            fclose(file);
                        });
                    }
                }
            } else {
                // attribute_name_index is out of valid range
                ERROR_AND_CLEANUP("attribute_name_index out of range.", {
                    free_class_file(cf);
                    fclose(file);
                });
            }
        }
    }

    fclose(file);
    return cf;
}

void free_class_file(ClassFile *cf) {
    if (!cf) return;

    // Free constant pool
    if (cf->constant_pool) {
        for (int i = 0; i < cf->constant_pool_count; ++i) {
            cp_info *entry = &cf->constant_pool[i];
            if (entry->tag == CONSTANT_Utf8) {
                SAFE_FREE(entry->info.utf8_info.bytes);
            }
        }
    }

    // Free methods
    if (cf->methods) {
        for (int i = 0; i < cf->methods_count; ++i) {
            method_info *method = &cf->methods[i];
            if (method->code_attribute) {
                SAFE_FREE(method->code_attribute->code);
                SAFE_FREE(method->code_attribute);
            }
        }
    }

    SAFE_FREE(cf->constant_pool);
    SAFE_FREE(cf->methods);
    SAFE_FREE(cf);
}

static void initialize_vm(void) {
    DEBUG_PRINT("Initializing diyJVM...\n");
}

static void cleanup_vm(void) {
    DEBUG_PRINT("Cleaning up diyJVM...\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2 || argc > 3) {
        printf("Usage: %s [-d] <class file>\n", argv[0]);
        printf("Options:\n");
        printf("  -d    Enable debug output\n");
        return 1;
    }

    if (argc == 3 && strcmp(argv[1], "-d") == 0) {
        debug_mode = true;
        // shift argv so that argv[1] points to the <class file>
        argv[1] = argv[2];
    }

    initialize_vm();

    ClassFile *cf = read_class_file(argv[1]);
    if (!cf) {
        fprintf(stderr, "Failed to read class file: %s\n", argv[1]);
        cleanup_vm();
        return 1;
    }

    // Basic info
    printf("Class file: %s\n", argv[1]);
    printf("Magic: 0x%08X\n", cf->magic);
    printf("Version: %d.%d\n", cf->major_version, cf->minor_version);
    printf("Constant pool entries: %d\n", cf->constant_pool_count);
    printf("Methods: %d\n", cf->methods_count);

    // Clean up
    free_class_file(cf);
    cleanup_vm();
    return 0;
}