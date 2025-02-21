/* joystick_midi_map_full.c */
#pragma clang system_header

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <math.h>
#ifdef __APPLE__
#include "/opt/homebrew/include/hidapi/hidapi.h"
#include "/opt/homebrew/include/portmidi.h"
#else
#include <hidapi/hidapi.h>
#include <portmidi.h>
#endif

#define BUF_SIZE 65

// Estructura para almacenar una combinación única de bytes del reporte HID
typedef struct {
    size_t len;
    unsigned char data[BUF_SIZE];
} ButtonCombo;

ButtonCombo* combos = NULL;
int combo_count = 0;
int combo_capacity = 0;

// Función para buscar una combinación ya registrada
int find_combo(const unsigned char *buf, size_t len) {
    for (int i = 0; i < combo_count; i++) {
        if (combos[i].len == len && memcmp(combos[i].data, buf, len) == 0) {
            return i;
        }
    }
    return -1;
}

// Almacena una nueva combinación y realiza realloc dinámico
void store_combo(const unsigned char *buf, size_t len) {
    if (combo_count >= combo_capacity) {
        combo_capacity = (combo_capacity == 0) ? 100 : combo_capacity * 2;
        ButtonCombo* tmp = realloc(combos, combo_capacity * sizeof(ButtonCombo));
        if (!tmp) {
            fprintf(stderr, "Error al asignar memoria.\n");
            exit(1);
        }
        combos = tmp;
    }
    combos[combo_count].len = len;
    memcpy(combos[combo_count].data, buf, len);
    combo_count++;
}

// Función para imprimir el buffer en formato hexadecimal a un stream
void print_buffer(const unsigned char *buf, size_t len, FILE *stream) {
    for (size_t i = 0; i < len; i++) {
        fprintf(stream, "%02X ", buf[i]);
    }
}

// Función para asignar una dirección basada en el eje del joystick izquierdo
// Asumimos que los valores del eje izquierdo están en la posición 1 y 2 (índices 1 y 2)
// Se usa un centro de 128 y un umbral para detectar movimiento
const char* assign_direction(const ButtonCombo *combo) {
    if (combo->len < 3) return "Desconocido";
    int centro = 128;
    int umbral = 15;
    int dx = (int)combo->data[1] - centro;
    int dy = (int)combo->data[2] - centro;
    if (abs(dx) < umbral && abs(dy) < umbral) {
        return "Neutral";
    }
    if (abs(dx) > abs(dy)) {
        return (dx > 0) ? "Derecha" : "Izquierda";
    } else {
        return (dy > 0) ? "Abajo" : "Arriba";
    }
}

// Función para exportar el resumen completo (manteniéndolo, pero no se usará en el resumen principal)
void export_summary() {
    FILE *fp = fopen("resumen_botones.txt", "w");
    if (fp == NULL) {
        fprintf(stderr, "Error al abrir el archivo de resumen.\n");
        return;
    }
    fprintf(fp, "---------- Resumen de botones registrados ----------\n");
    fprintf(fp, "Se han registrado %d combinaciones distintas.\n\n", combo_count);
    for (int i = 0; i < combo_count; i++) {
        fprintf(fp, "Botón %d: ", i);
        print_buffer(combos[i].data, combos[i].len, fp);
        fprintf(fp, "\t Dirección asignada: %s\n", assign_direction(&combos[i]));
    }
    fprintf(fp, "-----------------------------------------------------\n");
    fclose(fp);
    printf("Resumen exportado a 'resumen_botones.txt'\n");
}

// Función para exportar un resumen agrupado por los botones principales (direcciones)
void export_main_summary() {
    int count_arriba = 0, count_abajo = 0, count_izquierda = 0, count_derecha = 0, count_neutral = 0;
    for (int i = 0; i < combo_count; i++) {
        const char* dir = assign_direction(&combos[i]);
        if (strcmp(dir, "Arriba") == 0)
            count_arriba++;
        else if (strcmp(dir, "Abajo") == 0)
            count_abajo++;
        else if (strcmp(dir, "Izquierda") == 0)
            count_izquierda++;
        else if (strcmp(dir, "Derecha") == 0)
            count_derecha++;
        else
            count_neutral++;
    }
    FILE *fp = fopen("resumen_botones_principales.txt", "w");
    if (fp == NULL) {
        fprintf(stderr, "Error al abrir el archivo de resumen principal.\n");
        return;
    }
    fprintf(fp, "---------- Resumen de botones principales ----------\n");
    fprintf(fp, "Total de combinaciones: %d\n", combo_count);
    fprintf(fp, "Arriba: %d\n", count_arriba);
    fprintf(fp, "Abajo: %d\n", count_abajo);
    fprintf(fp, "Izquierda: %d\n", count_izquierda);
    fprintf(fp, "Derecha: %d\n", count_derecha);
    fprintf(fp, "Neutral: %d\n", count_neutral);
    fprintf(fp, "-----------------------------------------------------\n");
    fclose(fp);
    printf("Resumen principal exportado a 'resumen_botones_principales.txt'\n");
}

// Manejador de señales para capturar Ctrl+C y exportar el resumen principal
void sigint_handler(int signum) {
    export_main_summary();
    exit(0);
}

int main() {
    int res;
    unsigned char buf[BUF_SIZE];
    hid_device *handle;

    // Registrar el manejador de SIGINT para ejecutar al presionar Ctrl+C
    signal(SIGINT, sigint_handler);

    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el dispositivo HID usando Vendor ID y Product ID fijos
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    handle = hid_open(vendor_id, product_id, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        return -1;
    }

    // Bucle principal: leer continuamente el buffer del joystick
    while (1) {
        res = hid_read(handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            int btn = find_combo(buf, res);
            if (btn == -1) {
                btn = combo_count;
                store_combo(buf, res);
                // Imprimir solo cuando es una nueva combinación detectada
                printf("Nueva combinación detectada - Botón %d\n", btn);
            }
            // No imprimir las combinaciones ya registradas
        }
        usleep(1000); // pausa de 1 ms
    }

    hid_close(handle);
    hid_exit();
    return 0;
} 