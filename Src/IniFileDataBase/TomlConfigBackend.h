/**
 * @file IniFileDataBase/TomlConfigBackend.h
 * @brief C interface to the toml++-based serializer/parser behind the IniDataBase facade.
 * @details Keeps the in-memory INI database (`key=value` line model) unchanged while
 *          persisting to and loading from TOML. The C parser/serializer in IniDataBase.c
 *          drives these functions; toml++ (C++) handles all TOML quoting/escaping.
 * @author Martin Kiepfer martin.kiepfer@karcher.com
 * @copyright Alfred Kaercher SE & Co. KG
 */
#ifndef SRC_INIFILEDATABASE_TOMLCONFIGBACKEND_H
#define SRC_INIFILEDATABASE_TOMLCONFIGBACKEND_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Create an empty TOML document builder.
 * @return Opaque builder handle, or NULL on allocation failure. Free with TomlConfigBackendFree().
 */
void *TomlConfigBackendCreate(void);

/**
 * @brief Ensure a (possibly empty) section table exists in the builder.
 * @param[in] par_Handle Builder handle from TomlConfigBackendCreate(). NULL is ignored.
 * @param[in] par_Section Section name (used as a literal, quoted-if-needed TOML table key).
 */
void TomlConfigBackendAddSection(void *par_Handle, const char *par_Section);

/**
 * @brief Add one `key=value` entry line to a section.
 * @param[in] par_Handle Builder handle. NULL is ignored.
 * @param[in] par_Section Section the entry belongs to (created if missing).
 * @param[in] par_KeyValueLine Raw in-memory entry line; split into key/value at the first '='.
 *            A line without '=' is stored as a key with an empty value.
 */
void TomlConfigBackendAddEntry(void *par_Handle, const char *par_Section, const char *par_KeyValueLine);

/**
 * @brief Serialize the builder to a newly allocated TOML C string.
 * @param[in] par_Handle Builder handle.
 * @return Malloc'd, NUL-terminated TOML text (free with TomlConfigBackendFreeString()), or NULL on error.
 */
char *TomlConfigBackendSerialize(void *par_Handle);

/**
 * @brief Free a string returned by TomlConfigBackendSerialize().
 * @param[in] par_Text String to free. NULL is ignored.
 */
void TomlConfigBackendFreeString(char *par_Text);

/**
 * @brief Free a builder handle from TomlConfigBackendCreate().
 * @param[in] par_Handle Handle to free. NULL is ignored.
 */
void TomlConfigBackendFree(void *par_Handle);

/**
 * @brief Callback invoked once per section/key/value while parsing.
 * @param[in] par_Section Section name.
 * @param[in] par_Key Entry key.
 * @param[in] par_Value Entry value as text (non-string TOML scalars are stringified).
 * @param[in] par_Ctx Opaque context passed through from TomlConfigBackendParseBuffer().
 * @return 0 to continue parsing, non-zero to abort.
 */
typedef int (*TomlEntryCallback)(const char *par_Section, const char *par_Key,
                                 const char *par_Value, void *par_Ctx);

/**
 * @brief Parse a TOML document from memory and report each entry via a callback.
 * @param[in] par_Buffer TOML text (need not be NUL-terminated).
 * @param[in] par_Length Length of par_Buffer in bytes.
 * @param[in] par_Callback Invoked per top-level-table entry.
 * @param[in] par_Ctx Opaque context forwarded to the callback.
 * @return 0 on success, -1 on TOML parse error, -2 if the callback aborted.
 */
int TomlConfigBackendParseBuffer(const char *par_Buffer, size_t par_Length,
                                 TomlEntryCallback par_Callback, void *par_Ctx);

#ifdef __cplusplus
}
#endif

#endif /* SRC_INIFILEDATABASE_TOMLCONFIGBACKEND_H */
