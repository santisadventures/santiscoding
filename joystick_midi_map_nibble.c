/* joystick_midi_map_nibble.c */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <hidapi/hidapi.h>
#include <portmidi.h>

#define NIBBLE_BYTE_INDEX 5  // Usamos el byte 5 para leer el nibble de botones

// Función para enviar mensajes MIDI
void send_midi_message(PmStream *midi_stream, int status, int data1, int data2) {
    PmEvent event;
    event.message = Pm_Message(status, data1, data2);
    event.timestamp = 0;
    Pm_Write(midi_stream, &event, 1);
}

// Función para mapear el valor del nibble a una nota MIDI fija
int get_nibble_midi_note(int nibble) {
    switch(nibble) {
        case 0x01: return 30;
        case 0x02: return 31;
        case 0x04: return 32;
        case 0x08: return 33;
        default:   return -1;  // Combinaciones múltiples o no reconocidas
    }
}

// Función que lee la combinación completa del nibble y la mapea a un mensaje MIDI
void map_buttons_via_nibble(PmStream *midi_stream, unsigned char *buf, size_t len) {
    if (len <= NIBBLE_BYTE_INDEX) return;
    
    int nibble = buf[NIBBLE_BYTE_INDEX] & 0x0F;
    static int prev_nibble = -1;
    
    if (nibble != prev_nibble) {
        // Si había un valor previo válido, enviar Note Off
        if (prev_nibble != -1) {
            int prev_note = get_nibble_midi_note(prev_nibble);
            if (prev_note != -1) {
                send_midi_message(midi_stream, 0x80, prev_note, 0);
                printf("Liberado: valor nibble anterior 0x%X, nota %d\n", prev_nibble, prev_note);
            }
        }
        
        int note = get_nibble_midi_note(nibble);
        if (note != -1) {
            send_midi_message(midi_stream, 0x90, note, 127);
            printf("Presionado: valor nibble 0x%X, nota %d\n", nibble, note);
        } else {
            printf("Combinación múltiple o no reconocida detectada: 0x%X\n", nibble);
        }
        prev_nibble = nibble;
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
    
    pm_err = Pm_Initialize();
    if (pm_err != pmNoError) {
        fprintf(stderr, "Error al inicializar PortMidi: %s\n", Pm_GetErrorText(pm_err));
        hid_close(handle);
        return -1;
    }
    
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
    
    while (1) {
        res = hid_read(handle, buf, sizeof(buf));
        if (res < 0) {
            fprintf(stderr, "Error al leer del joystick\n");
            break;
        }
        if (res > 0) {
            map_buttons_via_nibble(midi_stream, buf, (size_t)res);
        }
        usleep(1000); // pausa de 1 ms
    }
    
    Pm_Close(midi_stream);
    Pm_Terminate();
    hid_close(handle);
    hid_exit();
    return 0;
} 