/* joystick_midi_map_buttons.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

// Función para enviar mensajes MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// Función que mapea cada botón a una nota MIDI individual, reconociendo cada botón por su bit en el reporte.
void map_buttons_to_midi(PmStream *midi_stream, unsigned char *buf, size_t len) {
    // Verificamos que el reporte tenga al menos 7 bytes (índice 5 y 6 deben existir)
    if(len < 7) return;
    
    // Almacenamos el estado anterior de 12 botones
    static int prev_buttons[12] = {0};
    int current_buttons[12];
    
    // Extraemos el estado de cada botón usando enmascaramiento bit a bit
    // Botones del byte 5
    current_buttons[0] = (buf[5] & 0x10) ? 1 : 0;
    current_buttons[1] = (buf[5] & 0x20) ? 1 : 0;
    current_buttons[2] = (buf[5] & 0x40) ? 1 : 0;
    current_buttons[3] = (buf[5] & 0x80) ? 1 : 0;
    // Botones del byte 6
    current_buttons[4] = (buf[6] & 0x01) ? 1 : 0;
    current_buttons[5] = (buf[6] & 0x02) ? 1 : 0;
    current_buttons[6] = (buf[6] & 0x04) ? 1 : 0;
    current_buttons[7] = (buf[6] & 0x08) ? 1 : 0;
    current_buttons[8] = (buf[6] & 0x10) ? 1 : 0;
    current_buttons[9] = (buf[6] & 0x20) ? 1 : 0;
    current_buttons[10] = (buf[6] & 0x40) ? 1 : 0;
    current_buttons[11] = (buf[6] & 0x80) ? 1 : 0;
    
    // Asignamos una nota MIDI única para cada botón, eligiendo notas fuera del rango 60-90
    int midi_notes[12] = {30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41};
    
    // Comparamos el estado actual con el anterior para detectar cambios
    for (int i = 0; i < 12; i++) {
        if (current_buttons[i] != prev_buttons[i]) {
            if (current_buttons[i]) {
                // Botón presionado, enviamos Note On
                send_midi_message(midi_stream, 0x90, midi_notes[i], 127);
                printf("Botón %d presionado. Nota: %d\n", i, midi_notes[i]);
            } else {
                // Botón liberado, enviamos Note Off
                send_midi_message(midi_stream, 0x80, midi_notes[i], 0);
                printf("Botón %d liberado. Nota: %d\n", i, midi_notes[i]);
            }
            prev_buttons[i] = current_buttons[i];
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

    // Bucle principal: leer datos del joystick y mapear botones a notas MIDI
    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            map_buttons_to_midi(midi_stream, buf, (size_t)res);
        }
        usleep(1000); // pausa de 1 ms
    }

    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
} 