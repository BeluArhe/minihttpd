#ifndef MIME_H
#define MIME_H

// Estructura para asociar extensiones con su tipo MIME
typedef struct {
    const char *extension;
    const char *mime_type;
} mime_mapping_t;

// Función para obtener el Content-Type basado en la extensión del archivo
const char *get_mime_type(const char *path);

#endif // MIME_H
