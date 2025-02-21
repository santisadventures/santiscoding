/* Nota: Los errores de #include que muestran 'cannot open source file "hidapi/hidapi.h"' y 'cannot open source file "portmidi.h"' son debidos a la configuración del includePath en el entorno de desarrollo y no afectan la compilación. */
#pragma clang system_header
//--------------------------------------------------------------------------------

// joystick_midi_debug.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

// Función para enviar mensajes MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

/* Agrego la función auxiliar para obtener el tiempo actual en milisegundos */
unsigned long get_current_millis() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000UL + tv.tv_usec / 1000;
}

// Defino constantes para el intervalo de repetición y la duración del pulso, en milisegundos
#define PULSE_INTERVAL 200
#define PULSE_DURATION 50

/* Agrego la función auxiliar para obtener la nota MIDI para cada botón (por bit), evitando el rango 60-90 */
int get_note_for_button(int button_index) {
    int candidate = 30 + button_index;
    if (candidate >= 60 && candidate <= 90) {
        candidate += 31;  // saltamos el rango 60-90
    }
    return candidate;
}

/* Versión revisada: mapea el reporte completo a una nota MIDI única basada en un hash */
void debug_map_report_to_note(PmStream *midi_stream, unsigned char *buf, size_t len) {
    // Imprimir el reporte completo para depuración
    printf("Reporte de entrada (len = %zu): ", len);
    for (size_t i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");

    // Calcular un hash simple de todo el reporte
    unsigned long hash = 0;
    for (size_t i = 0; i < len; i++) {
        hash = hash * 31 + buf[i];
    }

    // Mapear el hash a una nota MIDI: usar base 30 y evitar el rango 60-90
    int base_note = 30;
    int candidate = base_note + (hash % (128 - base_note));
    if (candidate >= 60 && candidate <= 90) {
        candidate += 31;
        if (candidate > 127) candidate = 127;
    }
    int note = candidate;

    // Mecanismo de pulso similar al anterior
    static unsigned long last_hash = 0;
    static int last_note = -1;
    static unsigned long last_triggered = 0;
    unsigned long now = get_current_millis();

    if (hash != last_hash) {
        if (last_note != -1) {
            printf("Cambio de reporte, enviando Note Off para nota %d\n", last_note);
            send_midi_message(midi_stream, 0x80, last_note, 0);
        }
        printf("Nuevo reporte, asignando Note On para nota %d\n", note);
        send_midi_message(midi_stream, 0x90, note, 127);
        last_hash = hash;
        last_note = note;
        last_triggered = now;
    } else {
        if ((now - last_triggered) >= PULSE_DURATION && last_note != -1) {
            printf("Finalizando pulso para nota %d\n", last_note);
            send_midi_message(midi_stream, 0x80, last_note, 0);
            last_note = -1;
        }
        if (last_note == -1 && (now - last_triggered) >= PULSE_INTERVAL) {
            printf("Re-triggering nota por estado persistente: %d\n", note);
            send_midi_message(midi_stream, 0x90, note, 127);
            last_triggered = now;
            last_note = note;
        }
    }
}

int main() {
    int res;
    unsigned char buf[65];
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

    // Usamos Vendor ID y Product ID fijos
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

    // Buscar el primer dispositivo MIDI de salida
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
        fprintf(stderr, "No se encontraron dispositivos MIDI de salida\n");
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

    // Bucle principal: leer continuamente del joystick y procesar inputs
    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            debug_map_report_to_note(midi_stream, buf, (size_t)res);
        }
        usleep(1000);
    }

    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
} 