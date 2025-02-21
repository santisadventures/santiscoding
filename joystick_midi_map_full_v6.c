/* joystick_midi_map_full_v6.c
 *
 * Integra lógica mejorada para joysticks y gatillos:
 * - Joysticks: 4 direcciones básicas (arriba, abajo, izquierda, derecha)
 * - Gatillos: Se corrige la lectura para que:
 *      LT se lea desde buf[7]
 *      RT se lea desde buf[8]
 * - Otros botones se registran si cambian.
 *
 * Versión 6: Soluciona el error de intercambio de los gatillos.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/select.h>
#include <stdint.h>
#ifdef __APPLE__
#include "/opt/homebrew/include/hidapi/hidapi.h"
#include "/opt/homebrew/include/portmidi.h"
#else
#include <hidapi/hidapi.h>
#include <portmidi.h>
#endif

#define BUF_SIZE 65
#define NOTE_BUF_SIZE 256
#define DEADZONE 8

typedef enum {
    Neutral,
    Up,
    Down,
    Left,
    Right
} JoystickDirection;

typedef struct {
    size_t len;
    unsigned char data[BUF_SIZE];
} ButtonCombo;

ButtonCombo* combos = NULL;
int combo_count = 0;
int combo_capacity = 0;

char **event_log = NULL;
int event_count = 0;
int event_capacity = 0;

JoystickDirection left_stick_state = Neutral;
JoystickDirection right_stick_state = Neutral;
int lt_pressed = 0;
int rt_pressed = 0;

void record_event(const char *event) {
    if (event_count >= event_capacity) {
        event_capacity = (event_capacity == 0) ? 100 : event_capacity * 2;
        char **tmp = realloc(event_log, event_capacity * sizeof(char *));
        if (!tmp) {
            fprintf(stderr, "Error al asignar memoria para el log de eventos.\n");
            exit(1);
        }
        event_log = tmp;
    }
    event_log[event_count] = strdup(event);
    event_count++;
}

JoystickDirection get_joystick_direction(uint8_t x, uint8_t y) {
    const uint8_t center = 0x80;
    if (x < center - DEADZONE) return Left;
    if (x > center + DEADZONE) return Right;
    if (y < center - DEADZONE) return Up;
    if (y > center + DEADZONE) return Down;
    return Neutral;
}

void interpret_hid_input(const unsigned char *buf) {
    // Se asume que el reporte tiene al menos 9 bytes.
    uint8_t lx = buf[1];
    uint8_t ly = buf[2];
    uint8_t rx = buf[3];
    uint8_t ry = buf[4];
    uint8_t lt = buf[7];  // Corrección: Gatillo Izquierdo
    uint8_t rt = buf[8];  // Corrección: Gatillo Derecho

    // Procesar Joystick Izquierdo
    JoystickDirection left_dir = get_joystick_direction(lx, ly);
    if (left_dir != left_stick_state) {
        left_stick_state = left_dir;
        switch (left_dir) {
            case Up:     printf("Joystick Izquierdo: Arriba\n"); break;
            case Down:   printf("Joystick Izquierdo: Abajo\n"); break;
            case Left:   printf("Joystick Izquierdo: Izquierda\n"); break;
            case Right:  printf("Joystick Izquierdo: Derecha\n"); break;
            case Neutral:printf("Joystick Izquierdo: Neutro\n"); break;
        }
    }

    // Procesar Joystick Derecho
    JoystickDirection right_dir = get_joystick_direction(rx, ry);
    if (right_dir != right_stick_state) {
        right_stick_state = right_dir;
        switch (right_dir) {
            case Up:     printf("Joystick Derecho: Arriba\n"); break;
            case Down:   printf("Joystick Derecho: Abajo\n"); break;
            case Left:   printf("Joystick Derecho: Izquierda\n"); break;
            case Right:  printf("Joystick Derecho: Derecha\n"); break;
            case Neutral:printf("Joystick Derecho: Neutro\n"); break;
        }
    }

    // Procesar Gatillo Izquierdo (LT)
    if (lt > 0 && !lt_pressed) {
        lt_pressed = 1;
        printf("Gatillo Izquierdo (LT) presionado\n");
    } else if (lt == 0 && lt_pressed) {
        lt_pressed = 0;
        printf("Gatillo Izquierdo (LT) liberado\n");
    }

    // Procesar Gatillo Derecho (RT)
    if (rt > 0 && !rt_pressed) {
        rt_pressed = 1;
        printf("Gatillo Derecho (RT) presionado\n");
    } else if (rt == 0 && rt_pressed) {
        rt_pressed = 0;
        printf("Gatillo Derecho (RT) liberado\n");
    }

    // Procesar otros botones (por ejemplo, bytes 5 y 6)
    if (buf[5] != 0x0F || buf[6] != 0x00) {
        printf("Botón extra detectado: %02X %02X\n", buf[5], buf[6]);
    }
}

void export_event_log() {
    FILE *fp = fopen("joystick_log_v6.txt", "w");
    if (fp == NULL) {
        fprintf(stderr, "Error al abrir el archivo de log para exportar.\n");
        return;
    }
    for (int i = 0; i < event_count; i++) {
        fprintf(fp, "%s\n", event_log[i]);
    }
    fclose(fp);
    printf("Log exportado a 'joystick_log_v6.txt'\n");
}

void sigint_handler(int signum) {
    export_event_log();
    exit(0);
}

int main() {
    int res;
    unsigned char buf[BUF_SIZE];
    hid_device *handle;

    signal(SIGINT, sigint_handler);

    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el dispositivo HID (Vendor y Product ID fijos)
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    handle = hid_open(vendor_id, product_id, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        hid_exit();
        return -1;
    }
    hid_set_nonblocking(handle, 1);

    printf("joystick_midi_map_full_v6 iniciado. Presione Ctrl+C para salir.\n");

    while(1) {
        res = hid_read(handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            interpret_hid_input(buf);
        }
        usleep(1000);  // Pausa para evitar alto consumo de CPU
    }

    export_event_log();
    hid_close(handle);
    hid_exit();
    return 0;
} 