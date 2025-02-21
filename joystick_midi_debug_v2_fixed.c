
/* joystick_midi_debug_v2_fixed.c
 *
 * Ahora lee todos los botones físicos, además de joysticks y gatillos.
 * Asigna notas MIDI únicas a cada evento y muestra los eventos en terminal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

#define BUF_SIZE 65
#define DEADZONE 8

typedef enum {
    Neutral,
    Up,
    Down,
    Left,
    Right
} JoystickDirection;

// Estado actual
JoystickDirection left_stick_state = Neutral;
JoystickDirection right_stick_state = Neutral;
int lt_pressed = 0;
int rt_pressed = 0;
uint16_t last_button_state = 0;

// Obtener tiempo actual en milisegundos
unsigned long get_current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000UL + tv.tv_usec / 1000;
}

// Enviar mensaje MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// Obtener dirección del joystick
JoystickDirection get_joystick_direction(uint8_t x, uint8_t y) {
    const uint8_t center = 0x80;
    if (x < center - DEADZONE) return Left;
    if (x > center + DEADZONE) return Right;
    if (y < center - DEADZONE) return Up;
    if (y > center + DEADZONE) return Down;
    return Neutral;
}

// Asignar notas MIDI por evento
int get_note_for_event(const char *event) {
    if (strcmp(event, "L_Up") == 0) return 40;
    if (strcmp(event, "L_Down") == 0) return 41;
    if (strcmp(event, "L_Left") == 0) return 42;
    if (strcmp(event, "L_Right") == 0) return 43;
    if (strcmp(event, "R_Up") == 0) return 44;
    if (strcmp(event, "R_Down") == 0) return 45;
    if (strcmp(event, "R_Left") == 0) return 46;
    if (strcmp(event, "R_Right") == 0) return 47;
    if (strcmp(event, "LT") == 0) return 48;
    if (strcmp(event, "RT") == 0) return 49;
    return 50; // Nota por defecto
}

// Interpretar entrada HID y enviar MIDI
void interpret_hid_input(PmStream *midi_stream, const unsigned char *buf) {
    uint8_t lx = buf[1];
    uint8_t ly = buf[2];
    uint8_t rx = buf[3];
    uint8_t ry = buf[4];
    uint8_t lt = buf[8];
    uint8_t rt = buf[7];

    // Leer botones desde buf[5] y buf[6]
    uint16_t button_state = (buf[6] << 8) | buf[5];

    // Detectar cambios en botones
    for (int i = 0; i < 16; i++) {
        int mask = 1 << i;
        int was_pressed = last_button_state & mask;
        int is_pressed = button_state & mask;

        if (is_pressed && !was_pressed) {
            int note = 60 + i;  // Notas MIDI a partir de 60 para botones
            printf("Botón %d presionado (Nota %d)\n", i + 1, note);
            send_midi_message(midi_stream, 0x90, note, 127);  // Note On
        } else if (!is_pressed && was_pressed) {
            int note = 60 + i;
            printf("Botón %d liberado (Nota %d)\n", i + 1, note);
            send_midi_message(midi_stream, 0x80, note, 0);    // Note Off
        }
    }
    last_button_state = button_state;

    // Joystick Izquierdo
    JoystickDirection left_dir = get_joystick_direction(lx, ly);
    if (left_dir != left_stick_state) {
        left_stick_state = left_dir;
        const char *event = "";
        switch (left_dir) {
            case Up: event = "L_Up"; break;
            case Down: event = "L_Down"; break;
            case Left: event = "L_Left"; break;
            case Right: event = "L_Right"; break;
            case Neutral: event = "L_Neutral"; break;
        }
        if (left_dir != Neutral) {
            int note = get_note_for_event(event);
            printf("Joystick Izquierdo: %s (Nota %d)\n", event, note);
            send_midi_message(midi_stream, 0x90, note, 127); // Note On
        }
    }

    // Joystick Derecho
    JoystickDirection right_dir = get_joystick_direction(rx, ry);
    if (right_dir != right_stick_state) {
        right_stick_state = right_dir;
        const char *event = "";
        switch (right_dir) {
            case Up: event = "R_Up"; break;
            case Down: event = "R_Down"; break;
            case Left: event = "R_Left"; break;
            case Right: event = "R_Right"; break;
            case Neutral: event = "R_Neutral"; break;
        }
        if (right_dir != Neutral) {
            int note = get_note_for_event(event);
            printf("Joystick Derecho: %s (Nota %d)\n", event, note);
            send_midi_message(midi_stream, 0x90, note, 127); // Note On
        }
    }

    // Gatillo Izquierdo (LT)
    if (lt > 0 && !lt_pressed) {
        lt_pressed = 1;
        printf("Gatillo Izquierdo (LT) presionado\n");
        send_midi_message(midi_stream, 0x90, get_note_for_event("LT"), 127);
    } else if (lt == 0 && lt_pressed) {
        lt_pressed = 0;
        printf("Gatillo Izquierdo (LT) liberado\n");
        send_midi_message(midi_stream, 0x80, get_note_for_event("LT"), 0);
    }

    // Gatillo Derecho (RT)
    if (rt > 0 && !rt_pressed) {
        rt_pressed = 1;
        printf("Gatillo Derecho (RT) presionado\n");
        send_midi_message(midi_stream, 0x90, get_note_for_event("RT"), 127);
    } else if (rt == 0 && rt_pressed) {
        rt_pressed = 0;
        printf("Gatillo Derecho (RT) liberado\n");
        send_midi_message(midi_stream, 0x80, get_note_for_event("RT"), 0);
    }
}

int main() {
    int res;
    unsigned char buf[BUF_SIZE];
    hid_device *handle;
    PmStream *midi_stream;
    PmError pm_err;
    int midi_device_id = -1;
    int num_devices, i;
    const PmDeviceInfo *info;

    // Inicializar hidapi
    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Conectar al joystick
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    handle = hid_open(vendor_id, product_id, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        return -1;
    }

    // Inicializar PortMidi
    pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al inicializar PortMidi: %s\n", Pm_GetErrorText(pm_err));
        hid_close(handle);
        return -1;
    }

    // Buscar dispositivo MIDI
    num_devices = Pm_CountDevices();
    for (i = 0; i < num_devices; i++) {
        info = Pm_GetDeviceInfo(i);
        if (info != NULL && info->output) {
            midi_device_id = i;
            printf("Usando dispositivo MIDI: ID %d, Nombre %s, Interfaz %s\n", i, info->name, info->interf);
            break;
        }
    }
    if (midi_device_id == -1) {
        fprintf(stderr, "No se encontraron dispositivos MIDI\n");
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    pm_err = Pm_OpenOutput(&midi_stream, midi_device_id, NULL, 128, NULL, NULL, 0);
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al abrir el dispositivo MIDI: %s\n", Pm_GetErrorText(pm_err));
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    printf("Joystick MIDI Debug V2 (Fixed) iniciado. Presiona Ctrl+C para salir.\n");

    // Bucle principal
    while (1) {
        res = hid_read(handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            interpret_hid_input(midi_stream, buf);
        }
        usleep(1000);
    }

    // Cierre de recursos
    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
}
