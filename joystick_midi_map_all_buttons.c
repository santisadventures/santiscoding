/* joystick_midi_map_all_buttons.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

#define BUTTON_START_INDEX 5
#define MAX_BUTTONS 512  // Capacidad máxima de botones a mapear

// Función para enviar mensajes MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// Función para asignar una nota MIDI única a cada botón, evitando el rango 60-90.
int get_button_midi_note(int button_index) {
    int note = 30 + button_index;
    // Si la nota cae en el rango prohibido, se desplaza fuera del mismo
    if (note >= 60 && note <= 90) {
        note += 31;  // saltar el rango 60-90
        if (note > 127) note = 127;  // asegurar que no supere el máximo de nota MIDI
    }
    return note;
}

// Función que mapea todos los botones presentes en el reporte HID a notas MIDI individuales.
// Se asume que los estados de botón están contenidos a partir del byte BUTTON_START_INDEX.
void map_all_buttons_to_midi(PmStream *midi_stream, unsigned char *buf, size_t len) {
    if (len <= BUTTON_START_INDEX) return;

    int total_buttons = (len - BUTTON_START_INDEX) * 8;
    static int prev_state[MAX_BUTTONS] = {0};

    // Iterar sobre cada byte a partir de BUTTON_START_INDEX
    for (size_t i = BUTTON_START_INDEX; i < len; i++) {
        for (int bit = 0; bit < 8; bit++) {
            int button_index = (int)((i - BUTTON_START_INDEX) * 8 + bit);
            if (button_index >= total_buttons || button_index >= MAX_BUTTONS) break;  // seguridad
            
            int current = (buf[i] >> bit) & 0x01;
            if (current != prev_state[button_index]) {
                int note = get_button_midi_note(button_index);
                if (current) {
                    // Botón presionado: enviar Note On
                    send_midi_message(midi_stream, 0x90, note, 127);
                    printf("Botón %d presionado. Nota: %d\n", button_index, note);
                } else {
                    // Botón liberado: enviar Note Off
                    send_midi_message(midi_stream, 0x80, note, 0);
                    printf("Botón %d liberado. Nota: %d\n", button_index, note);
                }
                prev_state[button_index] = current;
            }
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

    // Abrir el dispositivo HID usando Vendor ID y Product ID fijos
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

    // Seleccionar el primer dispositivo MIDI de salida disponible
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

    // Bucle principal: leer datos del joystick y mapear todos los botones a notas MIDI
    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            map_all_buttons_to_midi(midi_stream, buf, (size_t)res);
        }
        usleep(1000); // pausa de 1 ms
    }

    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
} 