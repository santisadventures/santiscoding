#include <stdio.h>
#include <stdlib.h>
#include <hidapi/hidapi.h>

#define VENDOR_ID 0x1949  // Reemplaza con el ID de tu dispositivo
#define PRODUCT_ID 0x0402 // Reemplaza con el ID de tu dispositivo

int main(void) {
    hid_device *handle;

    // Inicializar hidapi
    if (hid_init() != 0) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el dispositivo HID
    handle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el dispositivo HID\n");
        hid_exit();
        return -1;
    }

    printf("Dispositivo HID abierto exitosamente.\n");

    // Cerrar el dispositivo y salir
    hid_close(handle);
    hid_exit();
    return 0;
} 