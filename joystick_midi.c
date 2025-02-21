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

// Función para mapear botones del joystick a MIDI
void map_joystick_to_midi(PmStream *midi_stream, unsigned char *buf, size_t len) {
    static int prev_buttons[12] = {0};
    
    // Ajusta esto según el protocolo de tu joystick
    int current_buttons[12] = {
        buf[2] & 0x01, buf[2] & 0x02, buf[2] & 0x04, buf[2] & 0x08,
        buf[2] & 0x10, buf[2] & 0x20, buf[2] & 0x40, buf[2] & 0x80,
        buf[3] & 0x01, buf[3] & 0x02, buf[3] & 0x04, buf[3] & 0x08
    };
    
    int midi_notes[12] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79};
    
    for (int i = 0; i < 12; i++) {
        if (current_buttons[i] != prev_buttons[i]) {
            send_midi_message(midi_stream, 0x90, midi_notes[i], current_buttons[i] ? 127 : 0);
            prev_buttons[i] = current_buttons[i];
        }
    }
}

int main() {
    unsigned char buf[65];
    hid_device *handle;
    PmStream *midi_stream;
    int midi_device_id = -1;

    // Inicializar HIDAPI
    if (hid_init()) {
        fprintf(stderr, "Error al inicializar HIDAPI\n");
        return -1;
    }

    // Abrir el dispositivo HID con el Vendor ID y Product ID correctos
    handle = hid_open(0x1949, 0x0402, NULL);
    if (!handle) {
        fprintf(stderr, "No se pudo abrir el dispositivo\n");
        hid_exit();
        return -1;
    }

    // Inicializar PortMidi
    PmError pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error en PortMidi: %s\n", Pm_GetErrorText(pm_err));
        hid_close(handle);
        return -1;
    }

    // Buscar dispositivo MIDI de salida
    int num_devices = Pm_CountDevices();
    for (int i = 0; i < num_devices; i++) {
        const PmDeviceInfo *info = Pm_GetDeviceInfo(i);
        if (info->output) {
            midi_device_id = i;
            printf("MIDI Output: %s\n", info->name);
            break;
        }
    }

    if (midi_device_id == -1) {
        fprintf(stderr, "No hay dispositivos MIDI de salida\n");
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    // Abrir flujo MIDI
    pm_err = Pm_OpenOutput(&midi_stream, midi_device_id, NULL, 128, NULL, NULL, 0);
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al abrir MIDI: %s\n", Pm_GetErrorText(pm_err));
        Pm_Terminate();
        hid_close(handle);
        return -1;
    }

    // Bucle principal de lectura
    while (1) {
        int res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error de lectura\n");
            break;
        }
        
        // Debug: Imprimir datos recibidos
        if (res > 0) {
            printf("Datos recibidos (%d bytes): ", res);
            for (int i = 0; i < res; i++) {
                printf("%02X ", buf[i]);
            }
            printf("\n");
            map_joystick_to_midi(midi_stream, buf, res);
        }
        usleep(1000); // Reducir latencia si es necesario
    }

    // Limpieza
    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
}
