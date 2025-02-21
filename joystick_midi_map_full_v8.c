
/* joystick_midi_map_full_v8.c
 *
 * Correcciones aplicadas:
 * - RT ahora se mapea correctamente al byte buf[7]
 * - LT se lee desde buf[8] y se muestra correctamente
 * - Separación completa de LT y RT
 * - Umbral de presión (≥ 0x80) aplicado en ambos gatillos
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
#else
#include <hidapi/hidapi.h>
#endif

#define BUF_SIZE 65
#define DEADZONE 8
#define TRIGGER_THRESHOLD 0x80  // Umbral para considerar gatillo presionado

typedef enum {
    Neutral,
    Up,
    Down,
    Left,
    Right
} JoystickDirection;

JoystickDirection left_stick_state = Neutral;
JoystickDirection right_stick_state = Neutral;
int lt_pressed = 0;
int rt_pressed = 0;
uint16_t last_button_state = 0;

void record_event(const char *event) {
    printf("%s\n", event);
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
    uint8_t lx = buf[1];
    uint8_t ly = buf[2];
    uint8_t rx = buf[3];
    uint8_t ry = buf[4];
    uint8_t rt = buf[7];  // Gatillo Derecho (RT) correctamente mapeado
    uint8_t lt = buf[8];  // Gatillo Izquierdo (LT) correctamente mapeado

    // Leer botones desde buf[5] y buf[6]
    uint16_t button_state = (buf[6] << 8) | buf[5];

    // Detectar cambios en botones (solo mostrar presionado)
    for (int i = 0; i < 16; i++) {
        int mask = 1 << i;
        int was_pressed = last_button_state & mask;
        int is_pressed = button_state & mask;

        if (is_pressed && !was_pressed) {
            printf("Botón %d presionado\n", i + 1);
        }
    }
    last_button_state = button_state;

    // Joystick Izquierdo (sin "Neutro")
    JoystickDirection left_dir = get_joystick_direction(lx, ly);
    if (left_dir != left_stick_state && left_dir != Neutral) {
        left_stick_state = left_dir;
        switch (left_dir) {
            case Up:    printf("Joystick Izquierdo: Arriba\n"); break;
            case Down:  printf("Joystick Izquierdo: Abajo\n"); break;
            case Left:  printf("Joystick Izquierdo: Izquierda\n"); break;
            case Right: printf("Joystick Izquierdo: Derecha\n"); break;
            default: break;
        }
    } else if (left_dir == Neutral) {
        left_stick_state = Neutral;
    }

    // Joystick Derecho (sin "Neutro")
    JoystickDirection right_dir = get_joystick_direction(rx, ry);
    if (right_dir != right_stick_state && right_dir != Neutral) {
        right_stick_state = right_dir;
        switch (right_dir) {
            case Up:    printf("Joystick Derecho: Arriba\n"); break;
            case Down:  printf("Joystick Derecho: Abajo\n"); break;
            case Left:  printf("Joystick Derecho: Izquierda\n"); break;
            case Right: printf("Joystick Derecho: Derecha\n"); break;
            default: break;
        }
    } else if (right_dir == Neutral) {
        right_stick_state = Neutral;
    }

    // Gatillo Izquierdo (LT) con umbral
    if (lt >= TRIGGER_THRESHOLD && !lt_pressed) {
        lt_pressed = 1;
        printf("Gatillo Izquierdo (LT) presionado\n");
    } else if (lt < TRIGGER_THRESHOLD && lt_pressed) {
        lt_pressed = 0;
    }

    // Gatillo Derecho (RT) con umbral
    if (rt >= TRIGGER_THRESHOLD && !rt_pressed) {
        rt_pressed = 1;
        printf("Gatillo Derecho (RT) presionado\n");
    } else if (rt < TRIGGER_THRESHOLD && rt_pressed) {
        rt_pressed = 0;
    }
}

void sigint_handler(int signum) {
    printf("\nSaliendo y cerrando la aplicación.\n");
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

    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    handle = hid_open(vendor_id, product_id, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        hid_exit();
        return -1;
    }

    hid_set_nonblocking(handle, 1);
    printf("Logger HID V8 iniciado. Presione Ctrl+C para salir.\n");

    while (1) {
        res = hid_read(handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            interpret_hid_input(buf);
        }
        usleep(1000); // Pausa breve para evitar alto consumo de CPU
    }

    hid_close(handle);
    hid_exit();
    return 0;
}
