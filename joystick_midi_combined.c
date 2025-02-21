/* 
 * joystick_midi_combined.c
 *
 * Combina la lectura del joystick (lógica de joystick_midi_map_full_v3.c)
 * con la asignación de notas MIDI (como en joystick_midi_debug.c).
 * 
 * Para cada evento (dirección del joystick, gatillos y botón extra)
 * se asigna un índice único que se mapea a una nota MIDI (a partir de 30,
 * evitando las notas entre 60 y 90). Además, se implementa un mecanismo
 * de pulso: al detectar un cambio se envía un Note On y, si el estado se
 * mantiene, se envía un Note Off tras PULSE_DURATION y se re-triggera
 * luego de PULSE_INTERVAL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <stdint.h>
#ifdef __APPLE__
#include "/opt/homebrew/include/hidapi/hidapi.h"
#include "/opt/homebrew/include/portmidi.h"
#else
#include <hidapi/hidapi.h>
#include <portmidi.h>
#endif

#define BUF_SIZE 65
#define DEADZONE 8
#define PULSE_INTERVAL 200
#define PULSE_DURATION 50

typedef enum {
    Neutral,
    Up,
    Down,
    Left,
    Right
} JoystickDirection;

static PmStream *g_midi_stream = NULL;
static hid_device *g_handle = NULL;

// ---------------------------------------------------------------------------
// Función para enviar mensajes MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// ---------------------------------------------------------------------------
// Función auxiliar para obtener el tiempo actual en milisegundos
unsigned long get_current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000UL + tv.tv_usec / 1000;
}

// ---------------------------------------------------------------------------
// Función para asignar la nota MIDI en base a un índice
// Se usa la base 30 y se saltan las notas entre 60 y 90.
int get_note_for_button(int button_index) {
    int candidate = 30 + button_index;
    if (candidate >= 60 && candidate <= 90) {
        candidate += 31;  // saltar rango 60-90
        if (candidate > 127) candidate = 127;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
// Función para determinar la dirección del joystick basándose en sus valores analógicos.
JoystickDirection get_joystick_direction(uint8_t x, uint8_t y) {
    const uint8_t center = 0x80;
    if (x < center - DEADZONE) return Left;
    if (x > center + DEADZONE) return Right;
    if (y < center - DEADZONE) return Up;
    if (y > center + DEADZONE) return Down;
    return Neutral;
}

// ---------------------------------------------------------------------------
// Función para procesar un evento MIDI con mecanismo de pulso.
// Parámetros:
//   current_index: índice actual del evento; si es -1 se considera inactivo.
//   last_event: estado (índice) anterior del evento.
//   last_note: nota MIDI actualmente activa (-1 si no hay).
//   last_triggered: tiempo del último trigger.
//   midi_stream: stream de salida MIDI.
void process_midi_event(int current_index, int *last_event, int *last_note, unsigned long *last_triggered, PmStream *midi_stream) {
    unsigned long now = get_current_millis();
    if (current_index != *last_event) {
        // Cambio de estado: si había nota activa se apaga
        if (*last_note != -1) {
            send_midi_message(midi_stream, 0x80, *last_note, 0);
        }
        *last_event = current_index;
        if (current_index != -1) {
            int note = get_note_for_button(current_index);
            send_midi_message(midi_stream, 0x90, note, 127);
            *last_note = note;
            *last_triggered = now;
        } else {
            *last_note = -1;
        }
    } else {
        if (current_index != -1) { // Evento activo y sin cambios
            if ((now - *last_triggered) >= PULSE_DURATION && *last_note != -1) {
                send_midi_message(midi_stream, 0x80, *last_note, 0);
                *last_note = -1;
            }
            if (*last_note == -1 && (now - *last_triggered) >= PULSE_INTERVAL) {
                int note = get_note_for_button(current_index);
                send_midi_message(midi_stream, 0x90, note, 127);
                *last_triggered = now;
                *last_note = note;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Manejador de señal para una salida limpia ante Ctrl+C.
void sigint_handler(int signum) {
    if (g_midi_stream) {
        Pm_Close(g_midi_stream);
        Pm_Terminate();
    }
    if (g_handle) {
        hid_close(g_handle);
        hid_exit();
    }
    printf("\nSaliendo...\n");
    exit(0);
}

// ---------------------------------------------------------------------------
// Función main: inicializa hidapi, PortMidi y ejecuta el bucle de lectura y mapeo.
int main() {
    int res;
    unsigned char buf[BUF_SIZE];
    PmStream *midi_stream = NULL;
    PmError pm_err;
    int midi_device_id = -1;
    int num_devices, i;
    const PmDeviceInfo *info;
    
    signal(SIGINT, sigint_handler);

    // Inicializar hidapi
    if (hid_init()) {
        fprintf(stderr, "Error al inicializar hidapi\n");
        return -1;
    }

    // Abrir el joystick (Vendor ID y Product ID fijos)
    unsigned short vendor_id = 0x1949;
    unsigned short product_id = 0x0402;
    g_handle = hid_open(vendor_id, product_id, NULL);
    if (!g_handle) {
        fprintf(stderr, "No se pudo abrir el joystick\n");
        return -1;
    }
    hid_set_nonblocking(g_handle, 1);

    // Inicializar PortMidi
    pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al inicializar PortMidi: %s\n", Pm_GetErrorText(pm_err));
        hid_close(g_handle);
        return -1;
    }

    // Buscar el primer dispositivo MIDI de salida
    num_devices = Pm_CountDevices();
    for (i = 0; i < num_devices; i++) {
        info = Pm_GetDeviceInfo(i);
        if (info && info->output) {
            midi_device_id = i;
            printf("Usando dispositivo MIDI: ID %d, Nombre %s\n", i, info->name);
            break;
        }
    }
    if (midi_device_id == -1) {
        fprintf(stderr, "No se encontraron dispositivos MIDI de salida\n");
        Pm_Terminate();
        hid_close(g_handle);
        return -1;
    }

    pm_err = Pm_OpenOutput(&midi_stream, midi_device_id, NULL, 128, NULL, NULL, 0);
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al abrir el dispositivo MIDI: %s\n", Pm_GetErrorText(pm_err));
        Pm_Terminate();
        hid_close(g_handle);
        return -1;
    }
    g_midi_stream = midi_stream;
    
    printf("Joystick MIDI Combined iniciado. Presione Ctrl+C para salir.\n");

    // Variables para gestionar el pulso de cada evento
    int ls_last_event = -1, ls_last_note = -1;
    unsigned long ls_last_triggered = 0;
    int rs_last_event = -1, rs_last_note = -1;
    unsigned long rs_last_triggered = 0;
    int lt_last_event = -1, lt_last_note = -1;
    unsigned long lt_last_triggered = 0;
    int rt_last_event = -1, rt_last_note = -1;
    unsigned long rt_last_triggered = 0;
    int extra_last_event = -1, extra_last_note = -1;
    unsigned long extra_last_triggered = 0;

    // Bucle principal: leer HID y procesar eventos
    while (1) {
        res = hid_read(g_handle, buf, BUF_SIZE);
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            // --- Procesar joystick izquierdo (bytes 1 y 2) ---
            JoystickDirection ls_dir = get_joystick_direction(buf[1], buf[2]);
            // Si Neutral, no se envía nota (-1); si no, usamos (estado - 1): Up=0, Down=1, Left=2, Right=3.
            int ls_index = (ls_dir == Neutral) ? -1 : ((int)ls_dir - 1);

            // --- Procesar joystick derecho (bytes 3 y 4) ---
            JoystickDirection rs_dir = get_joystick_direction(buf[3], buf[4]);
            // Offset +4 para que las notas sean distintas: Up->4, Down->5, Left->6, Right->7.
            int rs_index = (rs_dir == Neutral) ? -1 : (4 + ((int)rs_dir - 1));

            // --- Procesar gatillos ---
            // En este caso, si el valor es > 0 se considera presionado.
            uint8_t lt_val = buf[8]; // Gatillo izquierdo
            int lt_index = (lt_val > 0) ? 8 : -1;
            uint8_t rt_val = buf[7]; // Gatillo derecho
            int rt_index = (rt_val > 0) ? 9 : -1;

            // --- Procesar botón extra (bytes 5 y 6) ---
            int extra_index = ((buf[5] != 0x0F) || (buf[6] != 0x00)) ? 10 : -1;

            // Aplicar el mecanismo de pulso para cada evento
            process_midi_event(ls_index, &ls_last_event, &ls_last_note, &ls_last_triggered, midi_stream);
            process_midi_event(rs_index, &rs_last_event, &rs_last_note, &rs_last_triggered, midi_stream);
            process_midi_event(lt_index, &lt_last_event, &lt_last_note, &lt_last_triggered, midi_stream);
            process_midi_event(rt_index, &rt_last_event, &rt_last_note, &rt_last_triggered, midi_stream);
            process_midi_event(extra_index, &extra_last_event, &extra_last_note, &extra_last_triggered, midi_stream);
        }
        usleep(1000);
    }

    // Limpieza (normalmente no se alcanza debido al bucle infinito)
    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(g_handle);
    hid_exit();
    return 0;
} 