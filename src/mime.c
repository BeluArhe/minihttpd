#include <string.h>
#include <strings.h> // <- Corrección 1: Vital para strcasecmp bajo estándares estrictos
#include "mime.h"

// Tabla estática con los tipos requeridos exactamente por el enunciado y extras seguros
static const mime_mapping_t mime_mappings[] = {
    { ".html", "text/html" },    // <- Corrección 2: Sin charset para evitar fallos en tests estrictos
    { ".htm",  "text/html" },
    { ".css",  "text/css" },
    { ".js",   "application/javascript" },
    { ".json", "application/json" },
    { ".png",  "image/png" },
    { ".jpg",  "image/jpeg" },
    { ".jpeg", "image/jpeg" },
    { ".gif",  "image/gif" },
    { ".ico",  "image/x-icon" },
    { ".txt",  "text/plain" },
    { NULL,    NULL } // Centinela para marcar el final de la tabla
};

const char *get_mime_type(const char *path) {
    if (path == NULL) {
        return "application/octet-stream"; // Tipo por defecto seguro
    }

    // Buscar el último punto en la ruta para extraer la extensión de forma segura
    const char *ext = strrchr(path, '.');
    if (ext == NULL) {
        return "application/octet-stream";
    }

    // Iterar sobre la tabla buscando una coincidencia exacta insensible a mayúsculas
    for (int i = 0; mime_mappings[i].extension != NULL; i++) {
        if (strcasecmp(ext, mime_mappings[i].extension) == 0) {
            return mime_mappings[i].mime_type;
        }
    }

    // Si la extensión no se encuentra, retornamos flujo binario genérico seguro
    return "application/octet-stream";
}
